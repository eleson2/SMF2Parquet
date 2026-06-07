# SMF2Parquet — Build TODO

Detailed, ordered task list to implement the design in `DESIGN.md`. Checkboxes are sized
to be individually completable and reviewable. Types in scope: **30, 70, 71, 72, 73, 74,
75, 76, 77, 78, 79, 99, 113** + a generic **raw** table.

Legend: ⛔ = blocker for everything after it · 🔁 = repeated once per SMF type · ❓ = needs
a decision · ✅ = done.

---

## Progress (2026-05-28)

- ✅ **Reuse strategy = Option A**, implemented: 13 parse cores + `smf_section.h` moved to
  `MF-records-to-C/smf/` (namespace `smf`); SMF-parser readers reduced to ClickHouse-`Row`
  shims that include the shared cores; SMF-parser rebuilds green (behaviour byte-identical
  to baseline). SMF2Parquet skeleton builds and runs (counts types 30/70/99/250 from
  `gen_test_smf`, parses SMF30 via the shared core).
- ✅ **Subtype Migration done**: RMF types (70, 74, 75) migrated to subtype-specific table names
  (`smf70-1`, etc.).
- ✅ **Dynamic Subtype Routing done**: `SubtypeRouterSink` implemented and used for all stub types
  (71, 72, 73, 76, 77, 78, 79, 99, 113), ensuring every subtype encountered gets its own table.
- ✅ **Arrow/Parquet installed** in WSL (24.0.0, via the Apache APT repo).
- ✅ **Type-30 Parquet slice works end-to-end**: `cmake -DSMF2PARQUET_WITH_PARQUET=ON`
  builds `smf2parquet` + `pq_dump`; on `gen_test_smf` output it writes `out/smf30.parquet`
  (ZSTD, atomic temp→rename) and `pq_dump` reads it back — 19 cols, native temporal types
  (`timestamp[us,UTC]`, `date32`), correct values + lineage columns.
- ✅ **Fixed** the subtype-`u16` short-record bug in cores 30/70/74/75 — all 13 SMF-parser
  `smf_reader_tests` now PASS (previously aborted).
- ✅ **Phase 3 (partitioned writer) done**: rows route to
  `out/smf_type=<table>/smf_date=YYYY-MM-DD/<table>-<run_id>.parquet`, lazy per-(type,date)
  writers, atomic temp→rename, shared `CommonColumns` block.
- ✅ **Phase 6 done**: sinks for all 13 types (bespoke 30/70/74/75, header-only
  71/72/73/76/77/78/79/113, 99 + body_length) **+ `raw` fallback** for unknown types.
- ✅ **Phase 7 Started — SMF 72-3 Implemented**: Full Workload Activity parser + bespoke
  multi-row sink.
- ⏭ **Next:** Implement remaining RMF and hardware parsers (71, 73, 78-3, 113, etc.).

---

## Phase 0 — Decisions & environment

- [x] ✅ **Parser reuse strategy = Option A**, chosen and implemented (DESIGN §6) — parse
      cores promoted into `MF-records-to-C/smf/`; see Phase 2.
- [ ] ❓ Confirm output compression (ZSTD recommended) and partition granularity
      (`smf_date` day vs day+hour).
- [x] ✅ Arrow/Parquet installed in WSL — 24.0.0 via the Apache APT repo (Ubuntu noble has
      no `libarrow-dev` in universe). `find_package(Arrow/Parquet CONFIG)` resolves at
      `/usr/lib/x86_64-linux-gnu/cmake/{Arrow,Parquet}`.
- [x] ✅ Arrow/Parquet smoke-tested: `smf2parquet` (writer) + `pq_dump` (reader) build and
      round-trip a Parquet file under GCC 13.3.

## Phase 1 — Project skeleton & build

- [x] Directory layout: `src/` created (no `readers/` — Option A). `sinks/`, `tests/`,
      `schema/`, `docs/` to be added as their phases land.
- [x] `CMakeLists.txt`: 3.28, C++23, `add_subdirectory(../MF-records-to-C ... EXCLUDE_FROM_ALL)`
      with `MF_RECORDS_BUILD_TESTS/TOOLS OFF`. Arrow/Parquet `find_package` is behind the
      `SMF2PARQUET_WITH_PARQUET` option (OFF) so the skeleton builds without Arrow; flip ON
      for the sink layer.
- [x] `.gitignore` + `README.md` (points to DESIGN.md / TODO.md).
- [x] `src/main.cpp`: mmap + `mf::process_smf_file` + counting dispatcher. Built + ran green
      on `gen_test_smf` output (4 records: types 30/70/99/250).
- [x] Reused `MmapFile` helper from `MF-records-to-C/tools/smf_to_parquet.cpp` (copied into
      `src/main.cpp`; factor into a shared header when the tool/sink split appears).

## Phase 2 — Parser access (depends on Phase 0 decision)

*Option A (chosen):*
- [x] Split each `SMF-parser/src/smfNN_reader.h`: parse core (`SmfNNRecord`, section parsers,
      `read_smfNN`) now in `MF-records-to-C/smf/smfNN_reader.h` (namespace `smf`, no `ch_batch`);
      `SmfNNRow`/`to_rowbinary` stay in SMF-parser, including the shared core.
- [x] Moved `smf_section.h` into `MF-records-to-C/smf/`; deleted the SMF-parser copy.
- [x] Rebuilt SMF-parser (clean dir, Arrow-free via `MF_RECORDS_BUILD_TOOLS OFF`) — all
      targets compile/link; runtime behaviour identical to the pre-refactor binary.

*Option B (not used — Option A chosen).*

- [ ] Either way: confirm all 13 `read_smfNN()` compile and run standalone (parse a
      synthetic record, no ClickHouse symbols).

## Phase 3 — Partitioned writer & sink framework

- [ ] `PartitionedParquetWriter`: key `(record_type, smf_date)` → open `arrow::FileWriter`;
      lazily create partition dirs; write `*.parquet.tmp` then atomic rename on close;
      filename = `<type>-<batchTs>-<runid>-<seq>.parquet`. ZSTD, `store_schema()`.
- [ ] Batch buffering: default 64Ki rows/batch (as in `smf30_parquet_sink.h`); flush on
      threshold and on close.
- [ ] Common-column helpers: `smf_timestamp` (date+time→`timestamp[us,UTC]`), `smf_date`
      (`date32`), `system_id`/`subsystem_id`, `record_type`, `subtype`, `flags`,
      `source_file`, `ingest_ts`. Add date/time/TOD→Arrow conversion helpers (extend
      `mf::` helpers; do **not** reuse the int-YYYYMMDD form from the type-30 example).
- [ ] Column-descriptor mechanism (X-macro / small struct list) so each sink declares its
      columns once and derives schema + builders + append from it.
- [ ] Unit-test the writer: write 2 partitions across a midnight-straddling batch, read back
      with Arrow, assert partition split + atomic rename behaviour.

## Phase 4 — Type 30 end-to-end (reference vertical slice)

- [x] 🐞 Guarded the unconditional subtype `read_u16()` in cores 30/70/74/75
      (`if (r.can_read(2)) ...`). SMF-parser `smf_reader_tests`: all 13 PASS.
- [x] `src/sinks/smf30_parquet_sink.h` written on the new framework — native temporal
      columns (`timestamp[us,UTC]`/`date32`), common + lineage columns, batched ZSTD.
      (Single-row for type 30; the 1→N path is exercised by 74/75 in Phase 6.)
- [x] `src/sinks/parquet_table_writer.h` — atomic temp→rename single-file writer (the
      partition-aware `PartitionedParquetWriter` is still Phase 3).
- [x] Wired dispatcher case `30` → `read_smf30` → `Smf30ParquetSink`.
- [x] Converted `gen_test_smf` output; verified by reading back with `tools/pq_dump.cpp`
      (no DuckDB/pyarrow available — added an Arrow-based reader instead).
- [ ] **Freeze this as the template** the other types copy. Document the per-type recipe in
      `docs/new-type-sink.md`.

## Phase 5 — Generic raw table (lossless coverage) ✅

- [x] `src/sinks/raw_parquet_sink.h`: common columns + `body binary` (raw record bytes).
      Table `raw` (`smf_type=raw`), partitioned like the rest.
- [x] Dispatcher `default:` → `RawParquetSink`. (A per-record `parse_error` fallback for
      exceptions thrown by *known*-type parsers is still TODO — see Phase 10/observability.)
- [x] Verified: a type-250 record lands in `raw` intact (record_type=250, body bytes present).

## Phase 6 — Sinks for all 13 types ✅

- [x] Sinks for all 13 types, routed in `main.cpp`: bespoke **30/70/74/75** (74/75 multi-row,
      1→N), **99** (+ `body_length`), generic `HeaderOnlySink` for **71/72/73/76/77/78/79/113**.
- [x] Shared `CommonColumns` + `PartitionedTable<Builders>` keep each sink small.
- [ ] 🔁 Cross-check column intent against `SMF-parser/schema/smfNN.sql` (fold into Phase 7).
- [x] Test fixtures incl. a midnight-straddle / multi-date file (Phase 9) - **DONE**: `large` profile generates realistic LPAR data (CICS/DB2/Batch) for SYS1.
- [ ] Golden-Parquet tests: convert fixture → read back → assert schemas/values per type.
- [ ] DuckDB round-trip test (`hive_partitioning=true`, group-by per type).
- [ ] Edge cases: empty file, truncated final record, midnight straddle, unknown type.
- [ ] CMake test target (`ctest`); WSL build+test one-liner documented in README.
## Phase 7 — Parser Enrichment (Subtype Implementation)

Implement full field-level parsing and bespoke sinks for all prioritized types/subtypes.

### 7.1 Infrastructure & Fixes
- [x] ✅ **SMF 78-3**: Wire up existing `Smf78_3ParquetSink` in `main.cpp`.
- [x] ✅ **SMF 70-1**: Verify `zaap_online` / `ziip_online` offsets and add to sink.
- [x] ✅ **SMF 74-1**: Verify `storage_group` (SMF74SGN) offset and add to sink.

### 7.2 RMF Core Metrics
- [x] ✅ **SMF 71-1 (Paging Activity)**: Central/virtual storage paging.
- [x] ✅ **SMF 73-1 (Channel Path Activity)**: Channel path utilization.
- [x] ✅ **SMF 77-1 (Enqueue Activity)**: Contention metrics.
- [x] ✅ **SMF 74-4 (Coupling Facility)**: CF structure and link performance.

### 7.3 Modern & Advanced (z/OS 3.1 era)
- [x] ✅ **SMF 113 (Hardware Counters)**: CPI, L1/L2 cache misses (Subtypes 1 & 2).
- [x] ✅ **SMF 74-9 (PCIE Activity)**: zEDC and RoCE card performance.
- [x] ✅ **SMF 98 (High-Frequency Throughput)**: 5-second interval performance data.
- [ ] **SMF 1154 (Compliance Evidence)**: Compliance subtypes.

### 7.4 Specialty & Hardware Metrics
- [x] ✅ **SMF 74-5 (Cache Controller)**: DASD cache hit/miss statistics.
- [x] ✅ **SMF 74-8 (Enterprise Disk)**: DS8000 extent pool and rank stats.
- [x] ✅ **SMF 70-2 (Cryptographic Activity)**: Crypto Express card performance.

### 7.5 Monitor II (Real-time snapshots)
- [x] ✅ **SMF 79-1 (Address Space State)**: ASD metrics.
- [x] ✅ **SMF 79-2 (Address Space Resource)**: ARD metrics.
- [x] ✅ **SMF 76-1 (Paging/Swapping Activity)**: Storage frame counts.
- [x] ✅ **SMF 79-13 (System Events)**: Configuration changes.

---

## Phase 9 — Test Data & Regression
- [x] ✅ **Profile 'small'**: Hand-crafted edge cases (misaligned triplets, empty sections).
- [x] ✅ **Profile 'large'**: 24h interval simulation covering all enriched types.
- [ ] **Regression Suite**: Automate comparison of `pq_dump` output against golden files.

---

## Phase 10 — Integration with the batch/compaction lifecycle
- [ ] Document the contract the 24h/monthly compaction routines rely on: partition layout,
      file-naming, lineage columns, schema-version, atomic visibility.
- [ ] Confirm dedup story for reprocessed batches (lineage columns + downstream merge).
- [ ] (Deferred) Iceberg registration approach (§7.4): `add_files` vs compaction writes
      Iceberg directly.

---

## Cross-cutting / definition of done
- [ ] All 13 type tables + `smf_raw` produced from the multi-type fixture and queryable in
      DuckDB.
- [ ] No record in the input is silently lost (sum of per-type + raw + malformed = total).
- [ ] Schemas documented, column order fixed, evolution additive.
- [ ] Single `cmake --build` in WSL is green; `ctest` passes.
- [ ] DESIGN §6 decision implemented and reflected here.
