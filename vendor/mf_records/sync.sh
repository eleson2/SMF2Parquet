#!/usr/bin/env bash
# Re-sync the vendored mf_records headers from an upstream MF-records-to-C checkout.
# Usage: vendor/mf_records/sync.sh [path-to-MF-records-to-C]   (default: /mnt/g/projects/MF-records-to-C)
set -euo pipefail
SRC="${1:-/mnt/g/projects/MF-records-to-C}"
DST="$(cd "$(dirname "$0")" && pwd)"

if [ ! -f "$SRC/dataset_reader.h" ]; then
    echo "error: $SRC does not look like MF-records-to-C (no dataset_reader.h)" >&2
    exit 1
fi

mkdir -p "$DST/smf"
cp "$SRC"/dataset_reader.h "$SRC"/mf_types.h "$SRC"/codepages.h \
   "$SRC"/smf_reader.h "$SRC"/smf_sink.h "$SRC"/smf_file_reader.h "$DST"/
cp "$SRC"/smf/*.h "$DST"/smf/
echo "Synced mf_records headers from $SRC into $DST"
