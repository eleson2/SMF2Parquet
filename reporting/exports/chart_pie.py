#!/usr/bin/env python3
"""
chart_pie.py — render a standalone HTML pie chart from the DuckDB marts.

Default: CPU seconds by application (sum of cpu_sec per application). Writes a
self-contained .html (inline SVG, no JS/CDN, works offline) you open in a browser.

Examples:
  ../.venv/bin/python chart_pie.py --db ../dbt/marts.duckdb --out ./out
  ../.venv/bin/python chart_pie.py --db ../dbt/marts.duckdb --metric ziip_sec

Run with the project venv (it already has duckdb):  ../.venv/bin/python
"""
from __future__ import annotations
import argparse
import datetime as dt
import html
import math
import pathlib
import sys

import duckdb

# A readable categorical palette (repeats if there are more slices than colors).
PALETTE = ["#4e79a7", "#f28e2b", "#e15759", "#76b7b2", "#59a14f",
           "#edc948", "#b07aa1", "#ff9da7", "#9c755f", "#bab0ac"]


def fetch(con, dim: str, metric: str, table: str):
    rows = con.execute(
        f"SELECT {dim} AS k, sum({metric}) AS v "
        f"FROM {table} WHERE {metric} IS NOT NULL "
        f"GROUP BY 1 HAVING sum({metric}) > 0 ORDER BY v DESC"
    ).fetchall()
    return [(str(k), float(v)) for k, v in rows]


def svg_pie(data, metric: str, size: int = 440) -> str:
    total = sum(v for _, v in data) or 1.0
    cx = cy = size / 2
    r = size / 2 - 10
    start = -math.pi / 2  # start at 12 o'clock
    slices, legend = [], []
    for i, (label, value) in enumerate(data):
        frac = value / total
        end = start + frac * 2 * math.pi
        color = PALETTE[i % len(PALETTE)]
        # full circle if a single slice
        if len(data) == 1:
            slices.append(f'<circle cx="{cx}" cy="{cy}" r="{r}" fill="{color}"/>')
        else:
            x1, y1 = cx + r * math.cos(start), cy + r * math.sin(start)
            x2, y2 = cx + r * math.cos(end), cy + r * math.sin(end)
            large = 1 if (end - start) > math.pi else 0
            slices.append(
                f'<path d="M{cx:.2f},{cy:.2f} L{x1:.2f},{y1:.2f} '
                f'A{r:.2f},{r:.2f} 0 {large} 1 {x2:.2f},{y2:.2f} Z" '
                f'fill="{color}"><title>{html.escape(label)}: '
                f'{value:,.1f} ({frac*100:.1f}%)</title></path>'
            )
        # percentage label at slice mid-angle
        mid = (start + end) / 2
        lx, ly = cx + r * 0.6 * math.cos(mid), cy + r * 0.6 * math.sin(mid)
        if frac > 0.04:
            slices.append(
                f'<text x="{lx:.1f}" y="{ly:.1f}" font-size="13" fill="#fff" '
                f'text-anchor="middle" dominant-baseline="middle">{frac*100:.0f}%</text>'
            )
        legend.append(
            f'<div style="display:flex;align-items:center;margin:2px 0">'
            f'<span style="width:14px;height:14px;background:{color};'
            f'display:inline-block;margin-right:8px;border-radius:2px"></span>'
            f'<span>{html.escape(label)} &mdash; {value:,.1f} '
            f'({frac*100:.1f}%)</span></div>'
        )
        start = end
    return (
        f'<svg width="{size}" height="{size}" viewBox="0 0 {size} {size}">'
        + "".join(slices) + "</svg>",
        "".join(legend),
        total,
    )


def main() -> int:
    ap = argparse.ArgumentParser(description="HTML pie chart from the DuckDB marts.")
    ap.add_argument("--db", default="../dbt/marts.duckdb", help="path to marts.duckdb")
    ap.add_argument("--dim", default="application", help="category column")
    ap.add_argument("--metric", default="cpu_sec", help="measure to sum")
    ap.add_argument("--table", default="fct_job_consumption", help="source table")
    ap.add_argument("--out", default="./out", help="output directory")
    args = ap.parse_args()

    if not pathlib.Path(args.db).exists():
        sys.exit(f"marts DB not found: {args.db!r} (run `dbt build` first)")
    con = duckdb.connect(args.db, read_only=True)
    data = fetch(con, args.dim, args.metric, args.table)
    if not data:
        sys.exit(f"no non-zero {args.metric} by {args.dim} in {args.table}")

    svg, legend, total = svg_pie(data, args.metric)
    title = f"{args.metric} by {args.dim}"
    page = f"""<!doctype html><html><head><meta charset="utf-8">
<title>{html.escape(title)}</title>
<style>body{{font-family:system-ui,Arial,sans-serif;margin:24px;color:#222}}
.wrap{{display:flex;gap:32px;align-items:center;flex-wrap:wrap}}
h1{{font-size:18px}} .total{{color:#666;font-size:13px;margin-top:8px}}</style></head>
<body><h1>{html.escape(title)}</h1>
<div class="wrap"><div>{svg}</div>
<div><b>Legend</b>{legend}<div class="total">total {args.metric} = {total:,.1f}</div></div></div>
<div class="total">source: {html.escape(args.table)} &middot; generated {dt.datetime.now():%Y-%m-%d %H:%M}</div>
</body></html>"""

    out = pathlib.Path(args.out)
    out.mkdir(parents=True, exist_ok=True)
    dest = out / f"pie_{args.metric}_by_{args.dim}.html"
    dest.write_text(page, encoding="utf-8")
    print(f"wrote {dest}  ({len(data)} slices, total {args.metric}={total:,.1f})")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
