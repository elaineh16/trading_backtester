"""Experiments: strategy comparison, parameter sweeps, out-of-sample validation.

Every backtest runs in the C++ engine with the GIL released, so the thread
pool here runs them on all cores.
"""

from __future__ import annotations

import itertools
import os
import warnings
from concurrent.futures import ThreadPoolExecutor
from dataclasses import dataclass
from typing import Any, Iterable, Mapping, Sequence

import pandas as pd

import trading_engine as te

METRIC_COLUMNS = [
    "total_pnl", "return_pct", "realized_pnl", "fees", "sharpe", "max_drawdown", "max_drawdown_pct",
    "fills", "maker_fills", "orders_sent", "traded_notional", "pnl_per_notional_bps",
    "max_abs_position", "final_position", "buy_and_hold_return_pct", "wall_seconds",
]


def _workers(workers: int | None) -> int:
    return workers or max(1, os.cpu_count() or 1)


def param_grid(grid: Mapping[str, Sequence[float]]) -> list[dict[str, float]]:
    """Cartesian product of parameter values: {"a": [1, 2], "b": [3]} -> [{a:1,b:3}, {a:2,b:3}]."""
    keys = list(grid)
    return [dict(zip(keys, combo)) for combo in itertools.product(*(grid[k] for k in keys))]


def time_split(trades: pd.DataFrame, train_frac: float = 0.5) -> tuple[pd.DataFrame, pd.DataFrame]:
    """Chronological split. Never shuffle a time series."""
    if not 0 < train_frac < 1:
        raise ValueError("train_frac must be in (0, 1)")
    cut = int(len(trades) * train_frac)
    return trades.iloc[:cut], trades.iloc[cut:]


def compare(
    trades: pd.DataFrame,
    runs: Iterable[tuple[str, str, Mapping[str, float]]],
    workers: int | None = None,
    **config: Any,
) -> tuple[pd.DataFrame, dict[str, te.BacktestResult]]:
    """Run several (label, strategy, params) setups on the same trades and config.

    Returns a metrics table (one row per label) and the full results by label.
    """
    runs = list(runs)

    def one(run: tuple[str, str, Mapping[str, float]]) -> te.BacktestResult:
        label, strategy, params = run
        return te.backtest(trades, strategy, params, label=label, **config)

    with ThreadPoolExecutor(_workers(workers)) as pool:
        results = list(pool.map(one, runs))
    table = pd.DataFrame([{**{k: r.metrics[k] for k in METRIC_COLUMNS}} for r in results],
                         index=[r.label for r in results])
    table.index.name = "run"
    return table, {r.label: r for r in results}


def run_grid(
    trades: pd.DataFrame,
    strategy: str,
    grid: Mapping[str, Sequence[float]] | Sequence[Mapping[str, float]],
    workers: int | None = None,
    base_params: Mapping[str, float] | None = None,
    **config: Any,
) -> pd.DataFrame:
    """Backtest every parameter combination; one row per combination.

    Columns: one per swept parameter, then the metrics in METRIC_COLUMNS.
    """
    combos = param_grid(grid) if isinstance(grid, Mapping) else [dict(c) for c in grid]
    base = dict(base_params or {})
    cfg = te.make_config(record_fills=False, **config)  # fills not needed for a sweep

    def one(params: dict[str, float]) -> dict[str, Any]:
        r = te.backtest(trades, strategy, {**base, **params}, cfg)
        return {**params, **{k: r.metrics[k] for k in METRIC_COLUMNS}}

    with ThreadPoolExecutor(_workers(workers)) as pool:
        rows = list(pool.map(one, combos))
    return pd.DataFrame(rows)


def config_sweep(
    trades: pd.DataFrame,
    strategy: str,
    params: Mapping[str, float],
    option: str,
    values: Sequence[float],
    workers: int | None = None,
    **config: Any,
) -> pd.DataFrame:
    """Vary one engine/exchange setting (e.g. maker_bps, latency_ms) with fixed strategy params."""

    def one(v: float) -> dict[str, Any]:
        r = te.backtest(trades, strategy, params, **{**config, option: v, "record_fills": False})
        return {option: v, **{k: r.metrics[k] for k in METRIC_COLUMNS}}

    with ThreadPoolExecutor(_workers(workers)) as pool:
        rows = list(pool.map(one, values))
    return pd.DataFrame(rows)


@dataclass
class SplitResult:
    """Outcome of selecting parameters in-sample and checking them out-of-sample."""

    strategy: str
    metric: str
    best_params: dict[str, float]
    in_sample: pd.DataFrame       # every combo, scored on the train window
    out_of_sample: pd.DataFrame   # every combo, scored on the test window
    train_period: tuple[pd.Timestamp, pd.Timestamp]
    test_period: tuple[pd.Timestamp, pd.Timestamp]
    min_fills_met: bool = True    # False if no combo traded enough and the winner was picked anyway

    @property
    def param_names(self) -> list[str]:
        return [c for c in self.in_sample.columns if c not in METRIC_COLUMNS]

    def joined(self) -> pd.DataFrame:
        """One row per combo with in-sample and out-of-sample scores side by side."""
        keys = self.param_names
        a = self.in_sample[keys + [self.metric, "fills"]].rename(columns={self.metric: "in_sample", "fills": "is_fills"})
        b = self.out_of_sample[keys + [self.metric, "fills"]].rename(columns={self.metric: "out_of_sample", "fills": "oos_fills"})
        return a.merge(b, on=keys)

    def rank_correlation(self) -> float:
        """Spearman correlation of in-sample vs out-of-sample scores across combos.

        Near 1: the sweep found something stable. Near 0 or negative: the in-sample
        ranking is mostly noise, and the 'best' params are likely overfit.
        """
        j = self.joined()
        return float(j["in_sample"].rank().corr(j["out_of_sample"].rank()))

    def best_row(self) -> pd.Series:
        j = self.joined()
        mask = pd.Series(True, index=j.index)
        for k, v in self.best_params.items():
            mask &= j[k] == v
        return j[mask].iloc[0]

    def oos_percentile_of_best(self) -> float:
        """Where the in-sample winner lands among all combos out-of-sample (100 = best)."""
        j = self.joined()
        best = self.best_row()["out_of_sample"]
        return float(100.0 * (j["out_of_sample"] <= best).mean())


def select_and_validate(
    trades: pd.DataFrame,
    strategy: str,
    grid: Mapping[str, Sequence[float]],
    metric: str = "total_pnl",
    train_frac: float = 0.5,
    min_fills: int = 10,
    workers: int | None = None,
    base_params: Mapping[str, float] | None = None,
    **config: Any,
) -> SplitResult:
    """Pick parameters on the earlier part of the data, then score them on the later part.

    Combos with fewer than ``min_fills`` in-sample fills are not eligible to win,
    so a strategy can't 'win' by barely trading. ``base_params`` are held fixed.
    """
    train, test = time_split(trades, train_frac)
    is_df = run_grid(train, strategy, grid, workers, base_params, **config)
    oos_df = run_grid(test, strategy, grid, workers, base_params, **config)

    eligible = is_df[is_df["fills"] >= min_fills]
    min_fills_met = not eligible.empty
    if not min_fills_met:
        warnings.warn(f"{strategy}: no parameter combination reached {min_fills} fills in-sample; "
                      "selecting among all combos. Use more data.", stacklevel=2)
        eligible = is_df
    best = eligible.sort_values(metric, ascending=False).iloc[0]
    best_params = {k: float(best[k]) for k in grid}

    return SplitResult(
        strategy=strategy,
        metric=metric,
        best_params=best_params,
        in_sample=is_df,
        out_of_sample=oos_df,
        train_period=(train.index[0], train.index[-1]),
        test_period=(test.index[0], test.index[-1]),
        min_fills_met=min_fills_met,
    )
