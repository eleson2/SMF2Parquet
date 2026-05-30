#!/usr/bin/env python3
"""
csv_report.py — headless CSV export for Excel/other tools.

Runs a set of named SQL queries against the DuckDB marts file (or, with
--parquet, straight over the Parquet via reporting/duckdb/init.sql) and writes
one CSV per query into an output directory. Intended for cron — the "simple CSV
for Excel" half of the reporting requirement, independent of Superset.

Examples:
  # against the marts dbt builds:
  python3 csv_report.py --db ../dbt/marts.duckdb --out ./out
  # straight over the raw Parquet dataset:
  python3 csv_report.py --parquet /srv/smf/out --out ./out

Run with the project venv (it already has duckdb):  ../.venv/bin/python
"""
from __future__ import annotations
import argparse
import datetime as dt
import pathlib
import sys

import duckdb

# name -> SQL. Each becomes <name>-<YYYYMMDD>.csv. Edit / add freely.
REPORTS: dict[str, str] = {
    "consumption_daily": """
        SELECT smf_date, customer, application, app_group, system_id, job_name,
               step_runs, cpu_sec, ziip_sec, elapsed_sec, excp
        FROM agg_consumption_daily
        ORDER BY smf_date, cpu_sec DESC
    """,
    "top_consumers": """
        SELECT customer, application, job_name, program_name,
               count(*) AS runs, round(sum(cpu_sec), 2) AS total_cpu_sec,
               sum(excp_count) AS total_excp
        FROM fct_job_consumption
        GROUP BY ALL
        ORDER BY total_cpu_sec DESC
        LIMIT 100
    """,
    "consumption_by_application": """
        SELECT customer, application, app_group,
               round(sum(cpu_sec), 2) AS cpu_sec,
               round(sum(ziip_sec), 2) AS ziip_sec,
               sum(step_runs) AS step_runs
        FROM agg_consumption_daily
        GROUP BY ALL
        ORDER BY cpu_sec DESC
    """,
}


def connect(db: str | None, parquet: str | None) -> duckdb.DuckDBPyConnection:
    if parquet:
        con = duckdb.connect()  # in-memory; build views over the Parquet
        con.execute(f"SET VARIABLE data_root = '{parquet}';")
        init = pathlib.Path(__file__).resolve().parents[1] / "duckdb" / "init.sql"
        con.execute(init.read_text())
        # init.sql defines smfNN views; the REPORTS above expect the dbt marts,
        # so --parquet mode only supports queries written against smfNN views.
        return con
    if not db or not pathlib.Path(db).exists():
        sys.exit(f"marts DB not found: {db!r} (run `dbt build` first, or use --parquet)")
    return duckdb.connect(db, read_only=True)


def main() -> int:
    ap = argparse.ArgumentParser(description="Export SMF reports to CSV.")
    ap.add_argument("--db", help="path to marts.duckdb")
    ap.add_argument("--parquet", help="path to SMF2Parquet output dir (uses init.sql views)")
    ap.add_argument("--out", default="./out", help="output directory for CSV files")
    ap.add_argument("--only", help="comma-separated subset of report names")
    args = ap.parse_args()

    out = pathlib.Path(args.out)
    out.mkdir(parents=True, exist_ok=True)
    stamp = dt.date.today().strftime("%Y%m%d")
    con = connect(args.db, args.parquet)

    wanted = set(args.only.split(",")) if args.only else set(REPORTS)
    written = 0
    for name, sql in REPORTS.items():
        if name not in wanted:
            continue
        dest = out / f"{name}-{stamp}.csv"
        con.execute(
            f"COPY ({sql}) TO '{dest.as_posix()}' (HEADER, DELIMITER ',')"
        )
        rows = con.execute(f"SELECT count(*) FROM ({sql})").fetchone()[0]
        print(f"wrote {dest}  ({rows} rows)")
        written += 1

    if not written:
        sys.exit("no reports matched --only")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
