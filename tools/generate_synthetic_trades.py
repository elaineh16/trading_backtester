#!/usr/bin/env python3
"""Generate a synthetic crypto trade tape for tests, CI and demos.

This is NOT market data. Use tools/download_binance_trades.py for real results.
The generator gives the engine something with realistic *shape*:
  * a latent mid price with regime-switching volatility and trend
  * bid/ask bounce: prints land on the bid (seller-initiated) or the ask
  * order-flow imbalance that leans with the current trend
  * bursty Poisson arrivals and heavy-tailed trade sizes

Writes the engine's normalized format: ts_us,price,qty,buyer_is_maker,trade_id
"""

from __future__ import annotations

import argparse
from pathlib import Path

import numpy as np


def generate(n: int, seed: int = 1, start_price: float = 65_000.0, start_ts_us: int = 1_759_276_800_000_000,
             tick: float = 0.01, mean_gap_ms: float = 40.0) -> dict[str, np.ndarray]:
    rng = np.random.default_rng(seed)

    # Regimes: change roughly every ~20k trades.
    regime_len = rng.geometric(1 / 20_000, size=n // 1000 + 10)
    regime_id = np.repeat(np.arange(len(regime_len)), regime_len)[:n]
    if len(regime_id) < n:
        regime_id = np.pad(regime_id, (0, n - len(regime_id)), mode="edge")
    vol_bps = rng.choice([0.15, 0.3, 0.6], size=regime_id.max() + 1, p=[0.5, 0.35, 0.15])[regime_id]
    drift_bps = rng.choice([-0.004, 0.0, 0.004], size=regime_id.max() + 1, p=[0.25, 0.5, 0.25])[regime_id]

    log_ret = (drift_bps + vol_bps * rng.standard_normal(n)) * 1e-4
    mid = start_price * np.exp(np.cumsum(log_ret))

    # Aggressor side leans with the trend: P(buy) = 0.5 + 25 * drift (bps).
    p_buy = np.clip(0.5 + 25 * drift_bps, 0.2, 0.8)
    buyer_aggressor = rng.random(n) < p_buy

    half_spread = np.maximum(tick, mid * 0.5e-4)
    price = np.where(buyer_aggressor, mid + half_spread, mid - half_spread)
    price = np.round(price / tick) * tick

    # Bursty arrivals: gap scale varies with volatility regime.
    gaps_us = rng.exponential(mean_gap_ms * 1000 * (0.3 / vol_bps), size=n).astype(np.int64) + 1
    ts = start_ts_us + np.cumsum(gaps_us)

    qty = np.round(rng.lognormal(mean=np.log(0.004), sigma=1.3, size=n), 5) + 0.00001

    return {
        "ts_us": ts,
        "price": price,
        "qty": qty,
        "buyer_is_maker": (~buyer_aggressor).astype(np.int8),
        "trade_id": np.arange(1, n + 1),
    }


def main() -> None:
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("--trades", type=int, default=500_000)
    p.add_argument("--seed", type=int, default=1)
    p.add_argument("--out", default="data/trades/SYNTH-BTCUSDT.csv")
    a = p.parse_args()

    cols = generate(a.trades, seed=a.seed)
    out = Path(a.out)
    out.parent.mkdir(parents=True, exist_ok=True)
    data = np.column_stack([cols[k] for k in ("ts_us", "price", "qty", "buyer_is_maker", "trade_id")])
    np.savetxt(out, data, delimiter=",", fmt=["%d", "%.2f", "%.5f", "%d", "%d"],
               header="ts_us,price,qty,buyer_is_maker,trade_id", comments="")
    span_h = (cols["ts_us"][-1] - cols["ts_us"][0]) / 3.6e9
    print(f"wrote {a.trades:,} trades spanning {span_h:.1f} h to {out}")


if __name__ == "__main__":
    main()
