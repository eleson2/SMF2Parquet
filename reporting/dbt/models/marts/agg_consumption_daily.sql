-- agg_consumption_daily — pre-aggregated daily consumption for fast dashboards
-- and the scheduled customer report. Grain: day x customer x application x
-- system x job. Drill from customer -> application -> job -> (fct) step.
{{ config(materialized='table') }}

select
    smf_date,
    customer,
    application,
    app_group,
    system_id,
    job_name,
    count(*)                  as step_runs,
    round(sum(cpu_sec),   3)  as cpu_sec,       -- GP CPU (net seconds)
    round(sum(ziip_sec),  3)  as ziip_sec,      -- zIIP offload
    round(sum(tcb_sec),   3)  as tcb_sec,
    round(sum(srb_sec),   3)  as srb_sec,
    round(sum(elapsed_sec), 3) as elapsed_sec,
    sum(excp_count)           as excp
from {{ ref('fct_job_consumption') }}
group by all
