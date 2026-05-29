# SMF2Parquet

Split a downloaded SMF log into one Parquet table per SMF record type, for a
DuckDB / Iceberg reporting platform.

- **Design:** see [`DESIGN.md`](DESIGN.md).
- **Build plan / task list:** see [`TODO.md`](TODO.md).

## Status

Skeleton (Phase 1): memory-maps a RECFM=VB SMF file, dispatches on the record
type byte, and reports a per-type record count. Parses use the shared `smf::`
record cores in `MF-records-to-C/smf/` (one source of truth, also consumed by
the SMF-parser ClickHouse back end). The per-type Parquet sinks are added next.

## Build (WSL Ubuntu, GCC 13, CMake 3.28)

```bash
wsl -e bash -c "cd /mnt/g/projects/SMF2Parquet && cmake -B build && cmake --build build"
./build/smf2parquet <input.smf>
```

The Parquet sink layer requires Apache Arrow + Parquet:

```bash
sudo apt-get install -y libarrow-dev libparquet-dev
cmake -B build -DSMF2PARQUET_WITH_PARQUET=ON && cmake --build build
```

## Dependencies

- [`MF-records-to-C`](../MF-records-to-C) — header-only C++23: `mf::Reader`
  primitives + `smf::read_smfNN` parse cores (linked via CMake `add_subdirectory`).
- Apache Arrow + Parquet C++ (for the sink layer).
