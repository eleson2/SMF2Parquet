-- dim_application — resolve each (customer, job_name) to a business application.
--
-- THE METADATA EXTENSION POINT. Mapping rules live in the editable seed
-- seeds/map_job_application.csv (glob patterns, e.g. PROD* -> Billing). Edit the
-- CSV and re-run `dbt seed && dbt run` — no SMF re-ingest needed.
--
-- A job may match several patterns; we keep the MOST SPECIFIC (longest pattern).
-- Unmatched jobs fall back to '(unmapped)' so nothing is lost and gaps are visible.
-- When CICS (SMF 110) / DB2 (SMF 100-102) parsing ships, add a stg_ model + a
-- map_*_application.csv seed and UNION it here — the dimension extends with no rework.
{{ config(materialized='table') }}

with jobs as (
    select distinct customer, job_name
    from {{ ref('stg_smf30') }}
),

matched as (
    select
        j.customer,
        j.job_name,
        m.application,
        m.app_group,
        row_number() over (
            partition by j.customer, j.job_name
            order by length(m.job_pattern) desc      -- most specific wins
        ) as rn
    from jobs j
    join {{ ref('map_job_application') }} m
      on j.job_name like replace(m.job_pattern, '*', '%')
)

select
    j.customer || '|' || j.job_name        as app_key,
    j.customer,
    j.job_name,
    coalesce(m.application, '(unmapped)') as application,
    coalesce(m.app_group,   'Unassigned') as app_group
from jobs j
left join matched m
  on  j.customer = m.customer
  and j.job_name = m.job_name
  and m.rn = 1
