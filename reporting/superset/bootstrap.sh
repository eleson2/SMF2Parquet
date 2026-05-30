#!/usr/bin/env bash
# One-shot Superset initialiser (run by the `superset-init` compose service).
# Idempotent: safe to re-run. Creates the admin user, upgrades the metadata DB,
# initialises roles, and registers the DuckDB marts database as a connection.
set -euo pipefail

echo "[init] upgrading metadata DB…"
superset db upgrade

echo "[init] creating admin user (if absent)…"
superset fab create-admin \
  --username "${ADMIN_USERNAME:-admin}" \
  --firstname Admin --lastname User \
  --email "${ADMIN_EMAIL:-admin@example.com}" \
  --password "${ADMIN_PASSWORD:-admin}" || true

echo "[init] superset init (roles/perms)…"
superset init

# Register the DuckDB marts file as a database connection, read-only so multiple
# readers are safe while dbt rebuilds it. MARTS_DB_PATH is the in-container path.
echo "[init] registering DuckDB marts connection…"
DB_URI="duckdb:///${MARTS_DB_PATH:-/data/marts.duckdb}?access_mode=read_only"
superset set-database-uri -d "SMF Marts (DuckDB)" -u "$DB_URI" || \
  echo "[init] (connection may already exist — continuing)"

echo "[init] done."
