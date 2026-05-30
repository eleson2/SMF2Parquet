-- reporting/duckdb/queries.sql
-- Starter consumption / performance queries for analysts.
-- Prereqs:  .read reporting/duckdb/init.sql   (defines the smfNN views)
--
-- These run against SMF type 30 (job/step accounting) only — the data that is
-- field-verified today. RMF (70-79) queries follow once Phase 7 lands.
--
-- Columns available on `smf30` (see src/sinks/smf30_parquet_sink.h + common_columns.h):
--   smf_timestamp, smf_date, system_id, subsystem_id, record_type, subtype,
--   flags, source_file, ingest_ts,                       -- common + lineage
--   job_name, step_name, job_id, program_name,            -- identity
--   elapsed_sec, cpu_sec, tcb_sec, srb_sec, ziip_sec,     -- consumption (seconds)
--   excp_count,                                           -- I/O (EXCP)
--   customer, path_system_id, filename, year, month       -- path-derived (init.sql)

-- 1) Daily consumption by system and job — the backbone of the consumption report.
--    GP CPU = tcb+srb; zIIP offload shown separately. CPU is already net seconds.
SELECT
    smf_date,
    system_id,
    job_name,
    count(*)                         AS step_runs,
    round(sum(cpu_sec),   2)         AS cpu_sec,
    round(sum(ziip_sec),  2)         AS ziip_sec,
    round(sum(elapsed_sec), 2)       AS elapsed_sec,
    sum(excp_count)                  AS excp
FROM smf30
GROUP BY ALL
ORDER BY smf_date, cpu_sec DESC;

-- 2) Top 20 CPU consumers over the whole dataset (technician "who is heavy").
SELECT
    job_name,
    program_name,
    count(*)                  AS runs,
    round(sum(cpu_sec), 2)    AS total_cpu_sec,
    round(avg(cpu_sec), 3)    AS avg_cpu_sec,
    round(max(cpu_sec), 2)    AS max_cpu_sec,
    sum(excp_count)           AS total_excp
FROM smf30
GROUP BY job_name, program_name
ORDER BY total_cpu_sec DESC
LIMIT 20;

-- 3) Day-over-day movers — jobs whose daily CPU jumped the most vs the prior day.
WITH daily AS (
    SELECT smf_date, job_name, sum(cpu_sec) AS cpu_sec
    FROM smf30
    GROUP BY smf_date, job_name
)
SELECT
    smf_date,
    job_name,
    round(cpu_sec, 2)                                                   AS cpu_sec,
    round(lag(cpu_sec) OVER (PARTITION BY job_name ORDER BY smf_date), 2) AS prev_cpu_sec,
    round(cpu_sec - lag(cpu_sec) OVER (PARTITION BY job_name ORDER BY smf_date), 2) AS delta_sec
FROM daily
QUALIFY delta_sec IS NOT NULL
ORDER BY delta_sec DESC
LIMIT 20;

-- 4) Coverage / sanity — record volume per type per day (uses the raw + every view
--    would be heavier; this just proves smf30 ingestion looks right).
SELECT smf_date, system_id, count(*) AS smf30_rows, round(sum(cpu_sec),1) AS cpu_sec
FROM smf30
GROUP BY ALL
ORDER BY smf_date, system_id;
