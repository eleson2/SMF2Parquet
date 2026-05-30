-- fct_job_consumption — one row per SMF30 job/step run, enriched with the
-- business application. The grain that powers job/step drill-down in Superset.
{{ config(materialized='table') }}

select
    s.customer,
    d.application,
    d.app_group,
    s.system_id,
    s.smf_date,
    s.smf_timestamp,
    s.job_name,
    s.step_name,
    s.job_id,
    s.program_name,
    s.cpu_sec,
    s.tcb_sec,
    s.srb_sec,
    s.ziip_sec,
    s.elapsed_sec,
    s.excp_count,
    s.year,
    s.month,
    s.source_file,
    s.ingest_ts
from {{ ref('stg_smf30') }} s
left join {{ ref('dim_application') }} d
  on s.customer = d.customer
 and s.job_name = d.job_name
