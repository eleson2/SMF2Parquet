-- rpt_jobs_by_date — every job that ran on each date, INCLUDING jobs whose
-- consumption is zero (no filtering — that's the point of this report).
--
-- Grain: smf_date x customer x system x application x job (x program).
-- job_label renders an empty job name as '(no job name)' for display while
-- job_name keeps the raw value. Ordered heaviest-first within each day/system.
{{ config(materialized='table') }}

select
    smf_date,
    customer,
    system_id,
    application,
    app_group,
    job_name,
    coalesce(nullif(job_name, ''),     '(no job name)') as job_label,
    coalesce(nullif(program_name, ''), '(none)')        as program_name,
    count(*)                   as step_runs,
    round(sum(cpu_sec),   3)   as cpu_sec,       -- GP CPU (net seconds)
    round(sum(ziip_sec),  3)   as ziip_sec,      -- zIIP offload
    round(sum(tcb_sec),   3)   as tcb_sec,
    round(sum(srb_sec),   3)   as srb_sec,
    round(sum(elapsed_sec), 3) as elapsed_sec,
    sum(excp_count)            as excp
from {{ ref('fct_job_consumption') }}
group by all
order by smf_date, system_id, cpu_sec desc, job_name
