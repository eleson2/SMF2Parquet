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
RUN pip install --no-cache-dir \
      "duckdb==1.1.3" \
      "duckdb-engine==0.13.6" \
      "psycopg2-binary" \
      "Pillow"

USER superset
