# Vendored: MF-records-to-C (header-only)

A vendored copy of the header-only mainframe-record parsing library, so SMF2Parquet builds
**standalone** with no sibling-project dependency.

- Namespaces: `mf::` (reader primitives, EBCDIC/packed/date/time conversions, SMF header) and
  `smf::` (per-type `read_smfNN` parse cores + `smf_section.h`).
- Contents:
  - root: `dataset_reader.h`, `mf_types.h`, `codepages.h`, `smf_reader.h`, `smf_sink.h`,
    `smf_file_reader.h`
  - `smf/`: `smf_section.h` + `smf30..smf113_reader.h`
- Original source: `G:\projects\MF-records-to-C`  ·  vendored 2026-05-29.

## Re-sync from upstream (while it still exists)

From a WSL shell:

```bash
vendor/mf_records/sync.sh                       # defaults to /mnt/g/projects/MF-records-to-C
vendor/mf_records/sync.sh /path/to/MF-records-to-C
```

## Note

Once `MF-records-to-C` is deleted, **this vendored copy becomes the source of truth** — make
field-offset fixes (TODO Phase 7) directly here. Do not re-add a dependency on the sibling repo.
