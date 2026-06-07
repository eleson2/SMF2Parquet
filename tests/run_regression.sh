#!/bin/bash
# tests/run_regression.sh — run golden-file regression tests.
# Usage: ./tests/run_regression.sh [update]
#   If 'update' is passed, it overwrites the golden files.

set -e

# Always run from project root
cd "$(dirname "$0")/.."

# 1. Build
echo "Building..."
cmake -B build -DSMF2PARQUET_WITH_PARQUET=ON && cmake --build build

# 2. Setup clean test environment
rm -rf tests/tmp_output
mkdir -p tests/tmp_output

# 3. Generate stable test data
echo "Generating test data..."
./build/gen_test_smf tests/data/small/test.smf small
./build/gen_test_smf tests/data/edge/test.smf edge

# 4. Run conversion and verification
for profile in small edge; do
    echo "Processing profile: $profile"
    rm -rf "tests/tmp_output/$profile"
    mkdir -p "tests/tmp_output/$profile"
    
    ./build/smf2parquet "tests/data/$profile/test.smf" "tests/tmp_output/$profile" "INTERNAL"

    # Find the customer dir
    cust_dir="tests/tmp_output/$profile/INTERNAL"
    if [ ! -d "$cust_dir" ]; then continue; fi

    # Find every system/year/month and then the parquet files
    # The files are now TYPE-YYYYMMDD-RUNID.parquet
    find "$cust_dir" -name "*.parquet" | while read -r pq_file; do
        # Filename format: TYPE-YYYYMMDD-RUNID.parquet
        # TYPE can contain hyphens if it's a subtype (e.g., SMF70-1)
        # RUNID is YYYYMMDDThhmmssZ-PID
        filename=$(basename "$pq_file")
        
        # The date is always the second to last component if we split by hyphen,
        # but wait, RUNID also has a hyphen.
        # Let's use regex to find the 8-digit date.
        date_val=$(echo "$filename" | grep -oE '[0-9]{8}' | head -n 1)
        # Type name is everything before the date
        type_name=$(echo "$filename" | sed -E "s/-${date_val}-.*//")
        
        # system_id is the parent of year=...
        system_id=$(basename "$(dirname "$(dirname "$(dirname "$pq_file")")")")
        
        echo "  Verifying $type_name ($system_id / $date_val)..."
        
        # Dump content, mask volatile columns
        # Filenames are now MASKED_RUN_ID suffix
        # ingest_ts is volatile (current time)
        ./build/pq_dump "$pq_file" \
            | sed -E 's/[0-9]{8}T[0-9]{6}Z-[0-9]+/MASKED_RUN_ID/g' \
            | sed -E 's/202[6-9]-[0-9]{2}-[0-9]{2} [0-9]{2}:[0-9]{2}:[0-9]{2}\.[0-9]{6}Z/MASKED_TIMESTAMP/g' \
            | sed -E "s/tests\/data\/$profile\/test\.smf/test.smf/g" \
            > "tests/tmp_output/${profile}_${system_id}_${type_name}_${date_val}.dump"

        golden_file="tests/golden/${profile}_${system_id}_${type_name}_${date_val}.golden"
        
        if [ "$1" == "update" ]; then
            cp "tests/tmp_output/${profile}_${system_id}_${type_name}_${date_val}.dump" "$golden_file"
            echo "    Updated golden file."
        else
            if [ ! -f "$golden_file" ]; then
                echo "    ERROR: Golden file $golden_file not found. Run with 'update' first."
                exit 1
            fi
            if diff -u "$golden_file" "tests/tmp_output/${profile}_${system_id}_${type_name}_${date_val}.dump"; then
                echo "    PASS."
            else
                echo "    FAIL: Diffs found in $profile / $system_id / $type_name / $date_val"
                exit 1
            fi
        fi
    done
done

echo "Regression tests complete."
