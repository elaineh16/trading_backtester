"""Python interface to the C++ event-driven trading engine.

    import trading_engine as te

    trades = te.load_trades("data/trades/BTCUSDT-trades-2026-10-01.csv")
    res = te.backtest(trades, "market_maker", {"base_half_spread_bps": 3}, maker_bps=0.0)
    res.metrics["total_pnl"], res.equity.tail(), res.fills.head()

All simulation runs in C++. The GIL is released during a backtest, so
``concurrent.futures.ThreadPoolExecutor`` runs sweeps in parallel.
"""

from __future__ import annotations

from dataclasses import dataclass, field
from pathlib import Path
from typing import Any, Mapping, Union

import numpy as np
import pandas as pd

from ._core import EngineConfig, ExchangeConfig, RiskLimits, strategy_names
from ._core import load_trades as _load_trades
from ._core import run_arrays as _run_arrays
from ._core import run_file as _run_file

__all__ = [
    "BacktestResult",
    "EngineConfig",
    "ExchangeConfig",
    "RiskLimits",
    "backtest",
    "load_trades",
    "make_config",
    "strategy_names",
]

TRADE_COLUMNS = ("ts_us", "price", "qty", "buyer_is_maker")

# Friendly keyword -> (section, field, scale) for make_config.
_CONFIG_KEYS: dict[str, tuple[str | None, str, float]] = {
    "cash": (None, "initial_cash", 1.0),
    "initial_cash": (None, "initial_cash", 1.0),
    "sample_sec": (None, "equity_sample_us", 1e6),
    "record_fills": (None, "record_fills", 1.0),
    "max_position": ("risk", "max_position", 1.0),
    "max_order_qty": ("risk", "max_order_qty", 1.0),
    "max_open_orders": ("risk", "max_open_orders", 1.0),
    "maker_bps": ("exchange", "maker_fee_bps", 1.0),
    "taker_bps": ("exchange", "taker_fee_bps", 1.0),
    "slippage_bps": ("exchange", "market_slippage_bps", 1.0),
    "touch_ratio": ("exchange", "touch_fill_ratio", 1.0),
}


def make_config(base: EngineConfig | None = None, **kwargs: Any) -> EngineConfig:
    """Build an EngineConfig from friendly keywords.

    Keywords: cash, max_position, max_order_qty, max_open_orders, latency_ms,
    maker_bps, taker_bps, slippage_bps, touch_ratio, sample_sec, record_fills.
    """
    cfg = EngineConfig()
    if base is not None:
        cfg.initial_cash = base.initial_cash
        cfg.exchange = base.exchange
        cfg.risk = base.risk
        cfg.equity_sample_us = base.equity_sample_us
        cfg.portfolio_publish_us = base.portfolio_publish_us
        cfg.record_fills = base.record_fills

    # Sub-structs are returned by value from pybind11: edit copies, then assign back.
    exchange, risk = cfg.exchange, cfg.risk
    for key, value in kwargs.items():
        if key == "latency_ms":
            exchange.order_latency_us = int(value * 1000)
            exchange.cancel_latency_us = int(value * 1000)
            continue
        if key not in _CONFIG_KEYS:
            raise TypeError(f"unknown config option {key!r}; valid: latency_ms, {', '.join(sorted(_CONFIG_KEYS))}")
        section, attr, scale = _CONFIG_KEYS[key]
        target = {"exchange": exchange, "risk": risk, None: cfg}[section]
        current = getattr(target, attr)
        if isinstance(current, bool):
            setattr(target, attr, bool(value))
        elif isinstance(current, int):
            setattr(target, attr, int(round(value * scale)))
        else:
            setattr(target, attr, float(value) * scale)
    cfg.exchange, cfg.risk = exchange, risk
    return cfg


def config_to_dict(cfg: EngineConfig) -> dict[str, Any]:
    ex, rk = cfg.exchange, cfg.risk
    return {
        "cash": cfg.initial_cash,
        "max_position": rk.max_position,
        "max_order_qty": rk.max_order_qty,
        "max_open_orders": rk.max_open_orders,
        "latency_ms": ex.order_latency_us / 1000,
        "maker_bps": ex.maker_fee_bps,
        "taker_bps": ex.taker_fee_bps,
        "slippage_bps": ex.market_slippage_bps,
        "touch_ratio": ex.touch_fill_ratio,
        "sample_sec": cfg.equity_sample_us / 1e6,
    }


def load_trades(path: Union[str, Path]) -> pd.DataFrame:
    """Load a trades CSV with the C++ parser (Binance dump or normalized format).

    Returns columns ts_us, price, qty, buyer_is_maker, trade_id and a UTC
    DatetimeIndex named ``time``.
    """
    raw = _load_trades(str(path))
    skipped = raw.pop("lines_skipped")
    df = pd.DataFrame(raw)
    df.index = pd.to_datetime(df["ts_us"], unit="us", utc=True)
    df.index.name = "time"
    df.attrs["lines_skipped"] = skipped
    df.attrs["source"] = str(path)
    return df


@dataclass
class BacktestResult:
    strategy: str
    params: dict[str, float]
    config: dict[str, Any]
    metrics: dict[str, float]
    fills: pd.DataFrame
    equity: pd.DataFrame
    signals: pd.DataFrame | None = None
    label: str = ""

    @property
    def returns(self) -> pd.Series:
        """Per-sample equity returns."""
        return self.equity["equity"].pct_change().dropna()

    def summary(self) -> pd.Series:
        keys = [
            "total_pnl", "return_pct", "fees", "sharpe", "max_drawdown_pct", "fills", "maker_fills",
            "traded_notional", "pnl_per_notional_bps", "max_abs_position", "buy_and_hold_return_pct",
        ]
        return pd.Series({k: self.metrics[k] for k in keys}, name=self.label or self.strategy)

    def __repr__(self) -> str:
        m = self.metrics
        return (f"BacktestResult({self.label or self.strategy}: pnl={m['total_pnl']:.2f}, "
                f"sharpe={m['sharpe']:.2f}, fills={m['fills']}, trades={m['market_trades']})")


def _to_frame(cols: Mapping[str, np.ndarray]) -> pd.DataFrame:
    df = pd.DataFrame(dict(cols))
    df.index = pd.to_datetime(df["ts_us"], unit="us", utc=True)
    df.index.name = "time"
    return df


def backtest(
    trades: Union[pd.DataFrame, str, Path],
    strategy: str,
    params: Mapping[str, float] | None = None,
    config: EngineConfig | None = None,
    *,
    record_signals: bool = False,
    label: str = "",
    **config_kwargs: Any,
) -> BacktestResult:
    """Run one backtest in the C++ engine.

    ``trades`` is a DataFrame (from :func:`load_trades` or with the same
    columns) or a CSV path, which is streamed without loading into memory.
    Extra keywords go to :func:`make_config`, e.g. ``maker_bps=0, latency_ms=10``.
    """
    params = {k: float(v) for k, v in (params or {}).items()}
    cfg = make_config(config, **config_kwargs)

    if isinstance(trades, (str, Path)):
        raw = _run_file(str(trades), strategy, params, cfg, record_signals)
    else:
        missing = [c for c in TRADE_COLUMNS if c not in trades.columns]
        if missing:
            raise ValueError(f"trades is missing columns {missing}")
        raw = _run_arrays(
            np.ascontiguousarray(trades["ts_us"].to_numpy(np.int64)),
            np.ascontiguousarray(trades["price"].to_numpy(np.float64)),
            np.ascontiguousarray(trades["qty"].to_numpy(np.float64)),
            np.ascontiguousarray(trades["buyer_is_maker"].to_numpy(bool)),
            strategy, params, cfg, record_signals,
        )

    fills = _to_frame(raw["fills"])
    equity = _to_frame(raw["equity"])
    signals = None
    if "signals" in raw:
        s = raw["signals"]
        signals = _to_frame({"ts_us": s["ts_us"], "name": np.asarray(s["name"], dtype=object), "value": s["value"]})
    return BacktestResult(
        strategy=raw["strategy"], params=params, config=config_to_dict(cfg), metrics=dict(raw["metrics"]),
        fills=fills, equity=equity, signals=signals, label=label,
    )
