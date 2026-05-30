# SMF Reporting & Analysis

A freeware reporting/analysis layer on top of the Parquet dataset produced by
SMF2Parquet. Nothing here touches the C++ ingest; it reads the Parquet
**source of truth** and serves it for online drill-down, scheduled email reports
(PDF + CSV), and ad-hoc SQL.

```
SMF2Parquet (C++)  ──►  Parquet dataset  ──►  DuckDB  ──►  dbt marts (marts.duckdb)
                        (source of truth)                       │
                                                                ├─►  Superset (dashboards, drill-down, emailed PDF/CSV)
                                                                └─►  csv_report.py / DuckDB CLI (CSV for Excel)
```

**Stack (all OSS):** DuckDB (query), dbt-duckdb (modeling + metadata), Apache
Superset (BI + scheduled reports), Docker Compose (host). The data stays open
Parquet/DuckDB, so a commercial engine (Trino, Spark, Dremio, …) can be added
later without changing anything upstream.

Architecture & rationale: [`DESIGN.md`](DESIGN.md) (this layer) and
[`../DESIGN.md`](../DESIGN.md) (the ingest/collection layer).

---

## Layout

```
reporting/
  duckdb/init.sql        per-type views (smf30…raw) over the Parquet layout
  duckdb/queries.sql     starter consumption/performance queries
  dbt/                   staging → marts (fct/agg) + dim_application + seeds
  superset/              Dockerfile, config, init script (DuckDB + reports ready)
  exports/csv_report.py  headless CSV export for Excel
  docker-compose.yml     Superset + Postgres + Redis + worker + beat
```

## On-disk dataset layout (what the views read)

```
<data_root>/<customer>/<system_id>/year=YYYY/month=MM/<TYPE>-<YYYYMMDD>-<run_id>.parquet
```
`customer`/`system_id` are plain dirs; `year`/`month` are hive keys; the SMF type
(`SMF30`, …, `RAW`) is a filename prefix. `init.sql` and the dbt `read_smf` macro
both recover `customer` from the path and lift `year`/`month` via hive partitioning.

---

## 1. Ad-hoc SQL (no server) — fastest path

```bash
# install the DuckDB CLI once (single binary): https://duckdb.org/docs/installation/
duckdb -cmd "SET VARIABLE data_root='/srv/smf/out';" -init duckdb/init.sql
# then, in the prompt:
.read duckdb/queries.sql
```
Export any result to CSV with `COPY (…) TO 'file.csv' (HEADER)`.

## 2. Build the marts (dbt)

```bash
python3 -m venv .venv && . .venv/bin/activate
pip install dbt-duckdb
cd dbt
dbt build --profiles-dir . --project-dir . --vars "data_root: /srv/smf/out"
# -> writes marts.duckdb with: stg_smf30, dim_application,
#    fct_job_consumption, agg_consumption_daily  (and runs data tests)
```
Set `SMF_DATA_ROOT` instead of `--vars` to make it the default.

### Adding your own metadata (jobs/CICS/DB2 → application)

This is the extension point you asked for. Edit
[`dbt/seeds/map_job_application.csv`](dbt/seeds/map_job_application.csv):

```csv
job_pattern,application,app_group
PROD*,Billing,FinanceCo
PAY*,Payroll,FinanceCo
```
`job_pattern` is a glob (`*` = wildcard); the **most specific** match wins;
unmatched jobs become `(unmapped)`. Then:

```bash
dbt seed && dbt run      # re-maps everything — NO SMF re-ingest needed
```
When CICS (SMF 110) / DB2 (SMF 100–102) parsing ships upstream, add a
`stg_smf110` model + a `map_cics_application.csv` seed and UNION it into
`dim_application` — the dimension extends with no rework.

## 3. Superset (dashboards + drill-down)

```bash
cp .env.example .env          # then edit: secrets, SMTP, MARTS_DB_HOST_PATH, SMF_DATA_ROOT
docker compose build
docker compose up -d
# UI at http://localhost:8088  (admin / your ADMIN_PASSWORD)
```
The `superset-init` service registers the DuckDB marts as a **read-only**
connection ("SMF Marts (DuckDB)") so it stays readable while dbt rebuilds it.

**Build the first dashboards** (in the UI, then *Export* to `superset/exports/`
to keep them as code):

1. **Datasets** → add `agg_consumption_daily` and `fct_job_consumption`.
2. **Customer Consumption** dashboard:
   - Bar/time chart: `SUM(cpu_sec)` by `smf_date`, broken down by `application`.
   - Enable **Drill to detail** and **Drill by** so technicians go
     customer → application → job → step (`fct_job_consumption`).
   - Filters: `customer`, `system_id`, `smf_date` range.
3. **Top Consumers** dashboard: table of top-N `job_name` by `SUM(cpu_sec)`,
   plus high-EXCP and biggest day-over-day movers.

## 4. Scheduled email reports — PDF + CSV

- **PDF dashboards (Superset):** Settings → **Alerts & Reports** → *Report* →
  pick a dashboard, schedule (cron), recipients. Requires SMTP filled in `.env`
  and the worker/beat services running (they are, in compose). The image already
  bundles Chromium for rendering. A chart-based report can also attach **CSV**.
- **Standalone CSV (cron):** `exports/csv_report.py` writes Excel-ready CSVs:
  ```bash
  .venv/bin/python exports/csv_report.py --db dbt/marts.duckdb --out /srv/smf/reports/csv
  ```
  See [`exports/crontab.example`](exports/crontab.example) for hourly dbt rebuild
  + nightly CSV pack.

---

## Try it on the bundled fixture

```bash
# from repo root: build ingest + emit a sample dataset
wsl -e bash -lc "cd /mnt/g/projects/SMF2Parquet && ./build/gen_test_smf /tmp/t.smf && ./build/smf2parquet /tmp/t.smf reporting/sample_out"
cd reporting/dbt && ../.venv/bin/dbt build --profiles-dir . --project-dir .
../.venv/bin/python ../exports/csv_report.py --db marts.duckdb --out /tmp/csv && ls /tmp/csv
```
> Note: the synthetic generator emits empty job bodies (zero CPU) — structure is
> correct but values are placeholders. Point `data_root` at a real SMF extract
> (or richer fixtures, ingest TODO Phase 9) for meaningful numbers. RMF (70–79)
> dashboards wait on field-offset verification (DESIGN §12 / ingest TODO Phase 7).
