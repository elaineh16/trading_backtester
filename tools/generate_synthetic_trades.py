#!/usr/bin/env python3
"""Generate a synthetic crypto trade tape for tests, CI and demos.

This is NOT market data. Use tools/download_binance_trades.py for real results.
The tape follows a standard microstructure model so strategies face the same
trade-offs they would on a real tape:

  efficient price   log p*_t = random walk with regime-switching volatility and drift
  transient noise   u_t      = AR(1) deviation from p* (order-flow pressure that decays)
  observed mid      m_t      = p*_t * exp(u_t)
  prints            at m_t +/- half_spread; the aggressor side leans with the trend

The noise term is what makes short-horizon prices mean-revert, which is what a
market maker earns from. The drift regimes are what momentum earns from.
Neither is guaranteed to be profitable after costs: that is what the backtests test.
Set --noise-bps 0 for a pure random walk, on which market making loses by construction.

Writes the engine's normalized format: ts_us,price,qty,buyer_is_maker,trade_id
"""

from __future__ import annotations

import argparse
from pathlib import Path

import numpy as np


def ar1(eps: np.ndarray, phi: float) -> np.ndarray:
    """u_t = phi * u_{t-1} + eps_t, vectorized."""
    try:
        from scipy.signal import lfilter

        return lfilter([1.0], [1.0, -phi], eps)
    except ImportError:  # pragma: no cover - scipy is in requirements
        out = np.empty_like(eps)
        acc = 0.0
        for i, e in enumerate(eps):
            acc = phi * acc + e
            out[i] = acc
        return out


def generate(
    n: int,
    seed: int = 1,
    start_price: float = 65_000.0,
    start_ts_us: int = 1_759_276_800_000_000,  # 2025-10-01 00:00 UTC
    tick: float = 0.01,
    mean_gap_ms: float = 40.0,
    half_spread_bps: float = 0.5,
    noise_bps: float = 1.0,
    noise_half_life: float = 40.0,
) -> dict[str, np.ndarray]:
    rng = np.random.default_rng(seed)

    # Regimes change roughly every ~20k trades.
    regime_len = rng.geometric(1 / 20_000, size=n // 1000 + 10)
    regime_id = np.repeat(np.arange(len(regime_len)), regime_len)[:n]
    if len(regime_id) < n:
        regime_id = np.pad(regime_id, (0, n - len(regime_id)), mode="edge")
    n_regimes = regime_id.max() + 1
    vol_bps = rng.choice([0.15, 0.3, 0.6], size=n_regimes, p=[0.5, 0.35, 0.15])[regime_id]
    drift_bps = rng.choice([-0.004, 0.0, 0.004], size=n_regimes, p=[0.25, 0.5, 0.25])[regime_id]

    # Efficient price.
    log_eff = np.cumsum((drift_bps + vol_bps * rng.standard_normal(n)) * 1e-4)

    # Transient AR(1) noise with the requested stationary std and half-life (in trades).
    if noise_bps > 0:
        phi = 0.5 ** (1.0 / noise_half_life)
        innov_std = noise_bps * np.sqrt(1 - phi**2)
        noise = ar1(innov_std * rng.standard_normal(n), phi) * 1e-4
    else:
        noise = np.zeros(n)
    mid = start_price * np.exp(log_eff + noise)

    # Aggressor side leans with the trend, and against the noise (stretched
    # prices attract liquidity-providing flow): P(buy) = 0.5 + 25*drift - 0.05*noise_bps.
    p_buy = np.clip(0.5 + 25 * drift_bps - 0.05 * noise * 1e4, 0.15, 0.85)
    buyer_aggressor = rng.random(n) < p_buy

    half_spread = np.maximum(tick, mid * half_spread_bps * 1e-4)
    price = np.where(buyer_aggressor, mid + half_spread, mid - half_spread)
    price = np.round(price / tick) * tick

    # Bursty arrivals: busier in volatile regimes.
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


def write_csv(cols: dict[str, np.ndarray], out: Path) -> None:
    import pandas as pd

    out.parent.mkdir(parents=True, exist_ok=True)
    df = pd.DataFrame({k: cols[k] for k in ("ts_us", "price", "qty", "buyer_is_maker", "trade_id")})
    df.to_csv(out, index=False, float_format="%.5f")


def main() -> None:
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("--trades", type=int, default=500_000)
    p.add_argument("--seed", type=int, default=1)
    p.add_argument("--half-spread-bps", type=float, default=0.5)
    p.add_argument("--noise-bps", type=float, default=1.0, help="std of transient mean-reverting noise")
    p.add_argument("--noise-half-life", type=float, default=40.0, help="in trades")
    p.add_argument("--out", default="data/trades/SYNTH-BTCUSDT.csv")
    a = p.parse_args()

    cols = generate(a.trades, seed=a.seed, half_spread_bps=a.half_spread_bps, noise_bps=a.noise_bps,
                    noise_half_life=a.noise_half_life)
    out = Path(a.out)
    write_csv(cols, out)
    span_h = (cols["ts_us"][-1] - cols["ts_us"][0]) / 3.6e9
    move = 100 * (cols["price"][-1] / cols["price"][0] - 1)
    print(f"wrote {a.trades:,} trades spanning {span_h:.1f} h (price {move:+.2f}%) to {out}")


if __name__ == "__main__":
    main()
