-- reporting/duckdb/init.sql
-- DuckDB view layer over the SMF2Parquet output dataset.
--
-- On-disk layout produced by src/sinks/partitioned_table.h:
--   <data_root>/<customer>/<system_id>/year=YYYY/month=MM/<TYPE>-<YYYYMMDD>-<run_id>.parquet
-- where <TYPE> is the uppercased table name (SMF30, SMF70, ..., RAW).
--   * customer  and system_id are plain directory names (NOT hive key=value).
--   * year / month ARE hive partition keys (read with hive_partitioning=true).
--   * the SMF type is a filename PREFIX, not a directory.
--
-- system_id is also stored as a real column inside every file; `customer` is
-- only in the path, so we recover it (and the type/run_id) from `filename`.
--
-- Usage (analyst self-serve):
--   duckdb -cmd "SET VARIABLE data_root='/mnt/g/projects/SMF2Parquet/out';" -init reporting/duckdb/init.sql
-- or inside a session:
--   SET VARIABLE data_root = '/path/to/out';
--   .read reporting/duckdb/init.sql
--
-- Override the default below if you don't want to SET the variable each time.

-- Default data_root if the caller did not SET one. (current_setting trick:
-- getvariable returns NULL when unset; coalesce to a sensible default.)
SET VARIABLE data_root = coalesce(
    getvariable('data_root'),
    '/mnt/g/projects/SMF2Parquet/out'
);

-- One macro to keep every per-type view identical. DuckDB expands the glob at
-- query time; hive_partitioning lifts year/month; filename lets us recover the
-- path-encoded customer and the type prefix.
--   read_smf('SMF30') -> all type-30 rows across all customers/systems/months.
CREATE OR REPLACE MACRO read_smf(type_prefix) AS TABLE
    SELECT
        -- path-derived dimensions (not stored in the file)
        regexp_extract(filename, '([^/]+)/([^/]+)/year=', 1) AS customer,
        regexp_extract(filename, '([^/]+)/([^/]+)/year=', 2) AS path_system_id,
        filename,
        *
    FROM read_parquet(
        getvariable('data_root') || '/**/' || type_prefix || '-*.parquet',
        hive_partitioning = true,
        filename = true,
        union_by_name = true            -- tolerate additive schema evolution (DESIGN §9)
    );

-- Per-type views. Add a line here when a new type sink ships.
CREATE OR REPLACE VIEW smf30  AS SELECT * FROM read_smf('SMF30');
CREATE OR REPLACE VIEW smf70  AS SELECT * FROM read_smf('SMF70');
CREATE OR REPLACE VIEW smf71  AS SELECT * FROM read_smf('SMF71');
CREATE OR REPLACE VIEW smf72  AS SELECT * FROM read_smf('SMF72');
CREATE OR REPLACE VIEW smf73  AS SELECT * FROM read_smf('SMF73');
CREATE OR REPLACE VIEW smf74  AS SELECT * FROM read_smf('SMF74');
CREATE OR REPLACE VIEW smf75  AS SELECT * FROM read_smf('SMF75');
CREATE OR REPLACE VIEW smf76  AS SELECT * FROM read_smf('SMF76');
CREATE OR REPLACE VIEW smf77  AS SELECT * FROM read_smf('SMF77');
CREATE OR REPLACE VIEW smf78  AS SELECT * FROM read_smf('SMF78');
CREATE OR REPLACE VIEW smf79  AS SELECT * FROM read_smf('SMF79');
CREATE OR REPLACE VIEW smf99  AS SELECT * FROM read_smf('SMF99');
CREATE OR REPLACE VIEW smf113 AS SELECT * FROM read_smf('SMF113');
CREATE OR REPLACE VIEW raw    AS SELECT * FROM read_smf('RAW');
