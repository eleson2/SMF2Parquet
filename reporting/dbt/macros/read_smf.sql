{#-
  read_smf(type_prefix) — expand to a read_parquet(...) over the SMF2Parquet
  dataset for one SMF type. Mirrors reporting/duckdb/init.sql so the dbt models
  and the analyst-facing views read the data identically.

  Layout: <data_root>/<customer>/<system_id>/year=YYYY/month=MM/<TYPE>-*.parquet
    * hive_partitioning lifts year/month
    * filename=true lets staging recover the path-encoded customer
    * union_by_name tolerates additive schema evolution (DESIGN §9)
-#}
{% macro read_smf(type_prefix) %}
    read_parquet(
        '{{ var("data_root") }}/**/' || '{{ type_prefix }}' || '-*.parquet',
        hive_partitioning = true,
        filename = true,
        union_by_name = true
    )
{% endmacro %}
