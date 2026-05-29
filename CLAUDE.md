# SMF2Parquet — AI Development Guide

C++23 tool that splits a downloaded SMF log (IBM mainframe System Management Facility
data, RECFM=VB EBCDIC) into **one Parquet table per record type**, for a DuckDB / Apache
Iceberg reporting platform.

Full design: [`DESIGN.md`](DESIGN.md). Build plan & status: [`TODO.md`](TODO.md).

---

## Build & run (WSL Ubuntu 24.04 — GCC 13, CMake 3.28)

> The Claude Code **Bash tool here is Git Bash (Windows), not WSL** — `/mnt/g` only exists
> inside WSL. Always wrap build/test commands: `wsl -e bash -lc "cd /mnt/g/projects/... && ..."`.

```bash
# Parsing + per-type record counts only (no external deps):
wsl -e bash -lc "cd /mnt/g/projects/SMF2Parquet && cmake -B build && cmake --build build"
./build/smf2parquet <input.smf>

# Full Parquet sink layer (needs Apache Arrow + Parquet C++):
wsl -e bash -lc "cd /mnt/g/projects/SMF2Parquet && cmake -B build -DSMF2PARQUET_WITH_PARQUET=ON && cmake --build build"
./build/smf2parquet <input.smf> <out_dir>
./build/pq_dump <out_dir>/smf_type=smf30/smf_date=YYYY-MM-DD/smf30-<run>.parquet   # read-back verify
```

- **Arrow/Parquet** are installed in WSL (24.0.0, via the Apache APT repo). They are required
  **only** for the sink layer; the parsing path builds without them.
- Generate a test SMF file with the built-in generator (self-contained, no deps):
  `./build/gen_test_smf /tmp/test.smf` — emits all 13 types + an unknown type across two
  dates (also exercises the midnight-straddle partition split).

---

## What it does

```
mmap(input) → mf::process_smf_file → on_record(hdr, r)
            → dispatch on hdr.record_type
            → smf::read_smfNN(r.data)  (shared parse core)
            → per-type sink: Arrow columnar builders
            → PartitionedTable: ZSTD Parquet, one file per (type, smf_date), atomic temp→rename
```

Output (Hive-partitioned, DuckDB/Iceberg-friendly):
```
<out_dir>/smf_type=<table>/smf_date=YYYY-MM-DD/<table>-<run_id>.parquet
```
`run_id` = `YYYYMMDDThhmmssZ-<pid>`, unique per run (batches accumulate; downstream compaction
merges to 24h/monthly). Records with an implausible header date go to `smf_date=__null__`.

---

## File map

| Path | Purpose |
|------|---------|
| `src/main.cpp` | `MmapFile`, `Dispatcher` (routes all 13 types + `raw` fallback), `SinkSet`, run_id/lineage |
| `src/sinks/arrow_helpers.h` | `arrow_ok`, `ts_type()` (timestamp[us,UTC]), `append_ts/append_date`, `fin` |
| `src/sinks/time_convert.h` | SMF `MfDate`/`MfTime` → `date32` / `timestamp[us,UTC]`; `days_from_civil`/`civil_from_days` |
| `src/sinks/common_columns.h` | `CommonColumns` — the 9-column block every table shares (incl. lineage) |
| `src/sinks/parquet_table_writer.h` | one Parquet file, atomic temp→rename, ZSTD |
| `src/sinks/partitioned_table.h` | `PartitionedTable<Builders>` — routes rows to per-(type,date) writers |
| `src/sinks/smf30/70/74/75_parquet_sink.h` | bespoke sinks (74/75 are multi-row: 1 row per device/page DS) |
| `src/sinks/smf99_parquet_sink.h` | type 99 (+ `body_length`) |
| `src/sinks/header_only_sink.h` | generic header-only sink for stub types 71/72/73/76/77/78/79/113 |
| `src/sinks/raw_parquet_sink.h` | `raw` table: common columns + `body` (Binary) for unknown types |
| `tools/pq_dump.cpp` | Arrow read-back verifier (no duckdb/pyarrow in this env) |
| `tests/gen_test_smf.cpp` | self-contained SMF fixture generator (no deps); target `gen_test_smf` |
| `vendor/mf_records/` | vendored header-only parsing library (`mf::` + `smf::` cores); `SYNC.md`/`sync.sh` to re-sync |

---

## Dependencies — standalone

SMF2Parquet has **no sibling-project dependency** (it does not need MF-records-to-C or
SMF-parser to build). The `mf::` reader primitives and `smf::` parse cores are **vendored**
under `vendor/mf_records/` — a copy of the header-only MF-records-to-C library. The only
external dependency is Apache Arrow + Parquet, and only for the sink layer.

- Re-sync the vendored headers from an upstream checkout: `vendor/mf_records/sync.sh`
  (see `vendor/mf_records/SYNC.md`).
- If MF-records-to-C is deleted, the vendored copy is the source of truth — make parser /
  field-offset fixes (Phase 7) directly in `vendor/mf_records/`.
- Historically the cores were unified into MF-records-to-C ("Option A", DESIGN §6) and shared
  with the SMF-parser ClickHouse back end; this project now owns its copy for standalone use.

---

## Adding / enriching a type sink (the template)

The reference is `src/sinks/smf30_parquet_sink.h`. Each type defines a `XxxBuilders` struct:
1. members: `CommonColumns common;` + one Arrow builder per type-specific column;
2. `static std::shared_ptr<arrow::Schema> schema()` — `CommonColumns::add_fields(f)` then
   type fields (column order = file order);
3. `uint64_t append(const smf::XxxRecord&, std::string_view src, int64_t ingest_us)` —
   `common.append(...)` then type fields; **return the number of rows** (loop for multi-row);
4. `arrow::ArrayVector finish()` — `common.finish_into(c)` then `fin(...)` each type builder.

Wrap it in a `XxxParquetSink` holding `PartitionedTable<XxxBuilders>` and add a `case` in
`Dispatcher::on_record`. To enrich a stub type, replace its `HeaderOnlySink` usage with a
bespoke sink once its parse core (in `MF-records-to-C/smf/`) gains fields.

---

## Conventions & gotchas

- Namespaces: `mf::` (low-level reader), `smf::` (record parse cores), `s2p::` (this project).
- All Parquet sink code is guarded by `#ifdef SMF2PARQUET_WITH_PARQUET`; keep the no-Arrow
  build green.
- Temporal columns use **native Arrow types** (`timestamp[us,UTC]`, `date32`) — not int YYYYMMDD.
- The standalone clang LSP reports false `arrow/api.h`/`std`/`mf` "not found" errors (no `-I`
  / sysroot); the WSL GCC build is the source of truth.
- **Field accuracy:** the `smf::` cores (esp. 70–113) carry `TODO: verify offsets` — column
  *values* for those types are NOT yet trustworthy. See TODO Phase 7. Verify against
  https://ibm.github.io/IBM-SMF-Explorer/mappings/ and `SMF-parser/reference_doc/`.
- Stub types (71/72/73/76/77/78/79/113) emit header-only tables until their cores are fleshed out.
