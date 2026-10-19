#!/usr/bin/env python3
"""Download historical tick-level trades from Binance's public data archive.

No API key needed. Files come from https://data.binance.vision (spot "trades"),
one zip per symbol per day, and are extracted as CSVs the C++ engine reads directly.

Examples:
    python tools/download_binance_trades.py --symbol BTCUSDT --date 2026-10-01
    python tools/download_binance_trades.py --symbol ETHUSDT --start 2026-09-01 --end 2026-09-07

BTCUSDT prints roughly 1-3 million trades per day.
"""

from __future__ import annotations

import argparse
import hashlib
import io
import sys
import urllib.error
import urllib.request
import zipfile
from datetime import date, timedelta
from pathlib import Path

BASE = "https://data.binance.vision/data/spot/daily/trades"


def day_range(start: date, end: date):
    d = start
    while d <= end:
        yield d
        d += timedelta(days=1)


def fetch(url: str) -> bytes:
    req = urllib.request.Request(url, headers={"User-Agent": "trading-engine-downloader"})
    with urllib.request.urlopen(req, timeout=120) as resp:
        return resp.read()


def download_day(symbol: str, day: date, out_dir: Path, verify: bool = True) -> Path | None:
    name = f"{symbol}-trades-{day.isoformat()}"
    csv_path = out_dir / f"{name}.csv"
    if csv_path.exists():
        print(f"  {csv_path.name} already present, skipping")
        return csv_path

    url = f"{BASE}/{symbol}/{name}.zip"
    print(f"  downloading {url}")
    try:
        blob = fetch(url)
    except urllib.error.HTTPError as e:
        print(f"  ! {e.code} for {name} (not published yet, or bad symbol)", file=sys.stderr)
        return None

    if verify:
        try:
            expected = fetch(url + ".CHECKSUM").decode().split()[0]
            actual = hashlib.sha256(blob).hexdigest()
            if expected != actual:
                raise RuntimeError(f"checksum mismatch for {name}")
        except urllib.error.HTTPError:
            print("  (no checksum published, skipping verification)")

    with zipfile.ZipFile(io.BytesIO(blob)) as zf:
        member = zf.namelist()[0]
        csv_path.write_bytes(zf.read(member))
    print(f"  -> {csv_path} ({csv_path.stat().st_size / 1e6:.1f} MB)")
    return csv_path


def main() -> int:
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("--symbol", default="BTCUSDT")
    p.add_argument("--date", help="single day, YYYY-MM-DD")
    p.add_argument("--start", help="first day, YYYY-MM-DD")
    p.add_argument("--end", help="last day (inclusive), YYYY-MM-DD")
    p.add_argument("--out", default="data/trades")
    p.add_argument("--no-verify", action="store_true")
    a = p.parse_args()

    if a.date:
        start = end = date.fromisoformat(a.date)
    elif a.start:
        start = date.fromisoformat(a.start)
        end = date.fromisoformat(a.end) if a.end else start
    else:
        start = end = date.today() - timedelta(days=2)  # archive lags ~1 day

    out = Path(a.out)
    out.mkdir(parents=True, exist_ok=True)
    print(f"{a.symbol}: {start} -> {end}")
    ok = [download_day(a.symbol.upper(), d, out, not a.no_verify) for d in day_range(start, end)]
    return 0 if all(ok) else 1


if __name__ == "__main__":
    sys.exit(main())
