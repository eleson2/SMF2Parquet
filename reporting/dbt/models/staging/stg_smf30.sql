-- stg_smf30 — typed/renamed view over SMF type 30 (job/step accounting).
-- Recovers `customer` from the path; everything else is a stored column.
{{ config(materialized='view') }}

select
    -- path-derived
    regexp_extract(filename, '([^/]+)/([^/]+)/year=', 1) as customer,
    -- identity
    job_name,
    step_name,
    job_id,
    program_name,
    -- time
    smf_timestamp,
    smf_date,
    system_id,
    subsystem_id,
    -- consumption (seconds) / io
    cpu_sec,
    tcb_sec,
    srb_sec,
    ziip_sec,
    elapsed_sec,
    excp_count,
    -- partition + lineage
    year,
    month,
    source_file,
    ingest_ts
from {{ read_smf('SMF30') }}
