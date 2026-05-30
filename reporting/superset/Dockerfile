# Superset image extended with the DuckDB SQLAlchemy driver so it can query the
# marts.duckdb file dbt builds. Headless Chromium + the report deps are added so
# Alerts & Reports can render dashboards to PDF/PNG for scheduled email (Phase D).
ARG SUPERSET_VERSION=4.1.1
FROM apache/superset:${SUPERSET_VERSION}

USER root

# Chromium for the report screenshotter (PDF/PNG of dashboards).
RUN apt-get update -q \
 && apt-get install -yq --no-install-recommends chromium chromium-driver \
 && rm -rf /var/lib/apt/lists/*

# DuckDB engine + driver, Postgres driver (metadata DB), and the report extras.
# IMPORTANT: duckdb must be >= the version that WROTE marts.duckdb (1.5.3) — a
# DuckDB file cannot be opened by an older engine. Keep this in step with the
# version dbt/the CLI use (see reporting/README.md).
RUN pip install --no-cache-dir \
      "duckdb==1.5.3" \
      "duckdb-engine" \
      "psycopg2-binary" \
      "Pillow"

USER superset
