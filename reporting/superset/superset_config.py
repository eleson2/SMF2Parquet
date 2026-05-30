"""
Superset configuration for the SMF reporting stack.

Mounted into every Superset container at /app/pythonpath/superset_config.py.
Covers: metadata DB (Postgres), Celery (Redis) for async + scheduled reports,
the screenshotter (Chromium) for PDF/PNG, and SMTP for emailed reports.
Secrets come from the environment (see .env.example) — nothing secret is hard-coded.
"""
import os

# --- Core -------------------------------------------------------------------
SECRET_KEY = os.environ["SUPERSET_SECRET_KEY"]
SQLALCHEMY_DATABASE_URI = os.environ["SUPERSET_DATABASE_URI"]  # Postgres metadata DB
ROW_LIMIT = 100_000

# --- Caching / Celery broker (Redis) ----------------------------------------
REDIS_HOST = os.environ.get("REDIS_HOST", "redis")
REDIS_PORT = int(os.environ.get("REDIS_PORT", 6379))

CACHE_CONFIG = {
    "CACHE_TYPE": "RedisCache",
    "CACHE_DEFAULT_TIMEOUT": 300,
    "CACHE_KEY_PREFIX": "superset_",
    "CACHE_REDIS_HOST": REDIS_HOST,
    "CACHE_REDIS_PORT": REDIS_PORT,
    "CACHE_REDIS_DB": 1,
}
DATA_CACHE_CONFIG = CACHE_CONFIG


class CeleryConfig:
    broker_url = f"redis://{REDIS_HOST}:{REDIS_PORT}/0"
    result_backend = f"redis://{REDIS_HOST}:{REDIS_PORT}/0"
    imports = ("superset.sql_lab", "superset.tasks.scheduler")
    worker_prefetch_multiplier = 1
    task_acks_late = False
    # Beat schedule that drives Alerts & Reports (every minute it checks what's due).
    beat_schedule = {
        "reports.scheduler": {
            "task": "reports.scheduler",
            "schedule": 60.0,
        },
        "reports.prune_log": {
            "task": "reports.prune_log",
            "schedule": 3600.0,
        },
    }


CELERY_CONFIG = CeleryConfig

# --- Feature flags ----------------------------------------------------------
FEATURE_FLAGS = {
    "ALERT_REPORTS": True,            # scheduled email reports (Phase D)
    "DASHBOARD_RBAC": True,
    "DRILL_BY": True,                 # right-click drill-down
    "DRILL_TO_DETAIL": True,
}

# --- Scheduled reports: screenshotter + webdriver ---------------------------
# WEBDRIVER_BASEURL is how the worker reaches Superset to screenshot dashboards.
WEBDRIVER_BASEURL = os.environ.get("WEBDRIVER_BASEURL", "http://superset:8088/")
WEBDRIVER_BASEURL_USER_FRIENDLY = os.environ.get("SUPERSET_PUBLIC_URL", WEBDRIVER_BASEURL)
WEBDRIVER_TYPE = "chrome"
WEBDRIVER_OPTION_ARGS = [
    "--headless=new",
    "--no-sandbox",
    "--disable-gpu",
    "--disable-dev-shm-usage",
    "--window-size=1600,2000",
]
# Tell Superset where Chromium lives (installed in the Dockerfile).
from selenium.webdriver.chrome.options import Options as ChromeOptions  # noqa: E402


def _chrome_opts():
    o = ChromeOptions()
    for a in WEBDRIVER_OPTION_ARGS:
        o.add_argument(a)
    o.binary_location = os.environ.get("CHROME_BIN", "/usr/bin/chromium")
    return o


WEBDRIVER_CONFIGURATION = {"options": _chrome_opts()}

# --- Email (SMTP) for scheduled reports -------------------------------------
SMTP_HOST = os.environ.get("SMTP_HOST", "localhost")
SMTP_PORT = int(os.environ.get("SMTP_PORT", 25))
SMTP_STARTTLS = os.environ.get("SMTP_STARTTLS", "true").lower() == "true"
SMTP_SSL = os.environ.get("SMTP_SSL", "false").lower() == "true"
SMTP_USER = os.environ.get("SMTP_USER", "")
SMTP_PASSWORD = os.environ.get("SMTP_PASSWORD", "")
SMTP_MAIL_FROM = os.environ.get("SMTP_MAIL_FROM", "smf-reports@example.com")
EMAIL_REPORTS_SUBJECT_PREFIX = "[SMF] "
