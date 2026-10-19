"""Standard strategy study: market making vs. momentum on one trade tape.

    python -m research.report --data data/trades/BTCUSDT-trades-2026-10-01.csv --out results/report

Steps
  1. Chronological 50/50 split into train and test windows.
  2. Grid-search each strategy on train; pick the best by PnL after fees.
  3. Score every combo on test: in- vs out-of-sample rank correlation shows
     how much of the in-sample ranking is overfitting.
  4. Run the selected settings on the untouched test window, side by side.
  5. Cost sensitivity: market-maker PnL vs. maker fee, both strategies vs. latency.

Writes figures (PNG), tables (CSV), results.json and REPORT.md into --out.
"""

from __future__ import annotations

import argparse
import json
import time
from pathlib import Path
from typing import Any

import numpy as np
import pandas as pd

import trading_engine as te

from . import plotting as P
from .experiments import compare, config_sweep, select_and_validate

MM_GRID = {
    "base_half_spread_bps": [0.5, 1, 2, 3, 5, 8],
    "inventory_skew_bps": [0, 2, 5, 10],
    "vol_multiplier": [0, 1, 2, 3],
}
MOM_GRID = {
    "bar_seconds": [15, 60, 300],
    "fast_bars": [5, 10, 20],
    "slow_bars": [30, 60, 120],
    "band_bps": [2, 5, 10],
}
# Same maximum exposure for both strategies: 0.1 BTC.
MM_FIXED = {"order_qty": 0.01, "max_inventory": 0.1}
MOM_FIXED = {"order_qty": 0.1}


def _fmt_table(df: pd.DataFrame, floatfmt: str = ",.2f") -> str:
    """Markdown table without extra dependencies."""
    cols = [df.index.name or ""] + list(df.columns)
    lines = ["| " + " | ".join(str(c) for c in cols) + " |", "|" + "---|" * len(cols)]
    for idx, row in df.iterrows():
        cells = [str(idx)]
        for v in row:
            if isinstance(v, (float, np.floating)):
                cells.append(format(v, floatfmt))
            elif isinstance(v, (int, np.integer)):
                cells.append(f"{v:,}")
            else:
                cells.append(str(v))
        lines.append("| " + " | ".join(cells) + " |")
    return "\n".join(lines)


def run(data: str, out: str, config: dict[str, Any], train_frac: float = 0.5, quick: bool = False) -> dict[str, Any]:
    t0 = time.time()
    out_dir = Path(out)
    out_dir.mkdir(parents=True, exist_ok=True)

    trades = te.load_trades(data)
    print(f"loaded {len(trades):,} trades from {data} ({trades.index[0]} -> {trades.index[-1]})")

    mm_grid, mom_grid = MM_GRID, MOM_GRID
    if quick:  # smaller grids for CI / smoke tests
        mm_grid = {"base_half_spread_bps": [1, 3], "inventory_skew_bps": [0, 5], "vol_multiplier": [1]}
        mom_grid = {"bar_seconds": [15, 60], "fast_bars": [5], "slow_bars": [30], "band_bps": [2, 5]}

    # 1-3. Select on train, score on test.
    mm_split = select_and_validate(trades, "market_maker", mm_grid, train_frac=train_frac, min_fills=200,
                                   base_params=MM_FIXED, **config)
    mom_split = select_and_validate(trades, "momentum", mom_grid, train_frac=train_frac, min_fills=10,
                                    base_params=MOM_FIXED, **config)
    for s in (mm_split, mom_split):
        s.in_sample.to_csv(out_dir / f"sweep_{s.strategy}_train.csv", index=False)
        s.out_of_sample.to_csv(out_dir / f"sweep_{s.strategy}_test.csv", index=False)
        print(f"{s.strategy}: best in-sample {s.best_params}, rank corr IS/OOS {s.rank_correlation():+.2f}")

    # 4. Head-to-head on the untouched test window.
    cut = int(len(trades) * train_frac)
    test = trades.iloc[cut:]
    mm_params = {**MM_FIXED, **mm_split.best_params}
    mom_params = {**MOM_FIXED, **mom_split.best_params}
    table, results = compare(test, [("market_maker", "market_maker", mm_params), ("momentum", "momentum", mom_params)],
                             **config)
    mm_detail = te.backtest(test, "market_maker", mm_params, record_signals=True, label="market_maker", **config)
    results["market_maker"] = mm_detail
    table.to_csv(out_dir / "test_comparison.csv")

    # 5. Sensitivity on the test window with the selected params.
    fee_values = [-1.0, -0.5, 0.0, 0.25, 0.5, 1.0, 1.5, 2.0]
    latency_values = [1, 5, 20, 50, 100, 250] if not quick else [1, 50]
    cfg_no_fee = {k: v for k, v in config.items() if k != "maker_bps"}
    fee_df = config_sweep(test, "market_maker", mm_params, "maker_bps", fee_values, **cfg_no_fee)
    cfg_no_lat = {k: v for k, v in config.items() if k != "latency_ms"}
    lat_mm = config_sweep(test, "market_maker", mm_params, "latency_ms", latency_values, **cfg_no_lat)
    lat_mom = config_sweep(test, "momentum", mom_params, "latency_ms", latency_values, **cfg_no_lat)
    fee_df.to_csv(out_dir / "sensitivity_maker_fee.csv", index=False)
    lat_mm.assign(strategy="market_maker").to_csv(out_dir / "sensitivity_latency_mm.csv", index=False)
    lat_mom.assign(strategy="momentum").to_csv(out_dir / "sensitivity_latency_momentum.csv", index=False)

    # Figures.
    figs = {}
    figs["pnl"] = P.save(P.pnl_over_time(results, "Out-of-sample: market making vs. momentum"), str(out_dir / "pnl_test.png"))
    figs["oos"] = P.save(P.in_vs_out_of_sample({"Market maker": mm_split, "Momentum": mom_split}), str(out_dir / "in_vs_out_of_sample.png"))
    figs["mm_heatmap"] = P.save(
        P.sweep_heatmap(mm_split.in_sample, "base_half_spread_bps", "inventory_skew_bps", "total_pnl",
                        title="Market maker, train window: best PnL by spread × inventory skew"),
        str(out_dir / "heatmap_mm.png"))
    figs["mom_heatmap"] = P.save(
        P.sweep_heatmap(mom_split.in_sample, "bar_seconds", "slow_bars", "total_pnl",
                        title="Momentum, train window: best PnL by bar size × slow EMA"),
        str(out_dir / "heatmap_momentum.png"))
    figs["fee"] = P.save(
        P.sensitivity(fee_df, "maker_bps", "total_pnl", color=P.BLUE, xlabel="maker fee (bps, negative = rebate)",
                      title="Market maker PnL vs. maker fee (test window)"),
        str(out_dir / "mm_fee_sensitivity.png"))
    fig, ax = P.plt.subplots(figsize=(7, 4))
    P.sensitivity(lat_mm, "latency_ms", "pnl_per_notional_bps", label="market_maker", color=P.BLUE, ax=ax, mark_breakeven=False)
    P.sensitivity(lat_mom, "latency_ms", "pnl_per_notional_bps", label="momentum", color=P.ORANGE, ax=ax, mark_breakeven=False)
    ax.set_xscale("log")
    ax.set_xticks(latency_values, [f"{v:g}" for v in latency_values])
    ax.minorticks_off()
    ax.set_xlabel("order latency (ms, log scale)")
    ax.set_ylabel("PnL per $ traded (bps)")
    ax.set_title("Edge vs. latency (test window)")
    figs["latency"] = P.save(fig, str(out_dir / "latency_sensitivity.png"))

    # A busy 5-minute window for the microstructure zoom.
    fills = mm_detail.fills
    if len(fills) > 10:
        start = P.typical_window(fills, minutes=2)
        figs["mm_zoom"] = P.save(P.mm_microstructure(test, mm_detail, start, minutes=2), str(out_dir / "mm_zoom.png"))

    summary = {
        "data": str(data),
        "market_trades": int(len(trades)),
        "period": [str(trades.index[0]), str(trades.index[-1])],
        "train_period": [str(mm_split.train_period[0]), str(mm_split.train_period[1])],
        "test_period": [str(mm_split.test_period[0]), str(mm_split.test_period[1])],
        "config": te.config_to_dict(te.make_config(**config)),
        "selected_params": {"market_maker": mm_params, "momentum": mom_params},
        "rank_correlation": {"market_maker": mm_split.rank_correlation(), "momentum": mom_split.rank_correlation()},
        "oos_percentile_of_selected": {"market_maker": mm_split.oos_percentile_of_best(),
                                       "momentum": mom_split.oos_percentile_of_best()},
        "min_fills_met": {"market_maker": mm_split.min_fills_met, "momentum": mom_split.min_fills_met},
        "test_metrics": {k: results[k].metrics for k in ("market_maker", "momentum")},
        "backtests_run": int(len(mm_split.in_sample) * 2 + len(mom_split.in_sample) * 2 + 3
                             + len(fee_df) + len(lat_mm) + len(lat_mom)),
        "runtime_seconds": round(time.time() - t0, 1),
        "figures": {k: Path(v).name for k, v in figs.items()},
    }
    (out_dir / "results.json").write_text(json.dumps(summary, indent=2, default=float))
    (out_dir / "REPORT.md").write_text(_markdown(summary, table, fee_df, mm_split, mom_split))
    print(f"done in {summary['runtime_seconds']} s, {summary['backtests_run']} backtests -> {out_dir}/REPORT.md")
    return summary


def _markdown(s: dict[str, Any], table: pd.DataFrame, fee_df: pd.DataFrame, mm_split, mom_split) -> str:
    cols = {
        "total_pnl": "PnL after fees ($)",
        "fees": "Fees ($)",
        "pnl_per_notional_bps": "PnL / $ traded (bps)",
        "sharpe": "Sharpe (ann.)",
        "max_drawdown": "Max drawdown ($)",
        "fills": "Fills",
        "traded_notional": "Traded ($)",
        "max_abs_position": "Max |pos| (BTC)",
    }
    t = table[list(cols)].rename(columns=cols)
    t.index.name = "Strategy"
    fee_tbl = fee_df[["maker_bps", "total_pnl", "fees", "pnl_per_notional_bps", "fills"]].set_index("maker_bps")
    fee_tbl.columns = ["PnL after fees ($)", "Fees ($)", "PnL / $ traded (bps)", "Fills"]
    fee_tbl.index.name = "Maker fee (bps)"
    cfg = s["config"]
    f = s["figures"]
    caveats = []
    test_days = (pd.Timestamp(s["test_period"][1]) - pd.Timestamp(s["test_period"][0])).total_seconds() / 86400
    if test_days < 7:
        caveats.append(f"The test window is {test_days:.1f} days. Annualized Sharpe ratios from less than a week of "
                       "data are not meaningful; compare PnL, PnL per $ traded and drawdown instead.")
    for name in ("market_maker", "momentum"):
        fills = s["test_metrics"][name]["fills"]
        if fills < 100:
            caveats.append(f"{name} made only {fills} fills (~{fills // 2} round trips) out-of-sample: too few "
                           "to tell skill from luck. Use a longer data set before drawing conclusions.")
    for name, ok in s["min_fills_met"].items():
        if not ok:
            caveats.append(f"No {name} parameter set reached the minimum fill count in-sample, so its selection "
                           "is based on very few trades.")
    if "SYNTH" in s["data"].upper():
        caveats.insert(0, "**This run uses a synthetic tape** (tools/generate_synthetic_trades.py). It exercises the "
                          "pipeline; it says nothing about real markets. Re-run on Binance data for real results.")
    caveat_md = ("\n## Caveats\n\n" + "\n".join(f"* {c}" for c in caveats) + "\n") if caveats else ""
    return f"""# Strategy study: market making vs. momentum

* Data: `{s['data']}`: {s['market_trades']:,} trades, {s['period'][0]} → {s['period'][1]}
* Train: {s['train_period'][0]} → {s['train_period'][1]}
* Test (untouched during selection): {s['test_period'][0]} → {s['test_period'][1]}
* Costs: maker {cfg['maker_bps']} bps, taker {cfg['taker_bps']} bps, slippage {cfg['slippage_bps']} bps, latency {cfg['latency_ms']} ms
* Max exposure: 0.1 BTC for both strategies; risk limit {cfg['max_position']} BTC
* {s['backtests_run']} backtests in {s['runtime_seconds']} s

## Out-of-sample results (selected parameters)

{_fmt_table(t)}

Selected parameters:
* market maker: `{s['selected_params']['market_maker']}`
* momentum: `{s['selected_params']['momentum']}`

![PnL on the test window]({f['pnl']})
{caveat_md}
## Overfitting check

Every parameter combination was scored on both windows. Rank correlation between
in-sample and out-of-sample PnL: **market maker {s['rank_correlation']['market_maker']:+.2f}**,
**momentum {s['rank_correlation']['momentum']:+.2f}**. The in-sample winner ranked in the
{s['oos_percentile_of_selected']['market_maker']:.0f}th / {s['oos_percentile_of_selected']['momentum']:.0f}th
percentile out-of-sample (market maker / momentum).

![In-sample vs out-of-sample]({f['oos']})

## Parameter sweeps (train window)

![Market maker heatmap]({f['mm_heatmap']})

![Momentum heatmap]({f['mom_heatmap']})

## Cost sensitivity

Market making earns a few basis points per trade at best, so its PnL depends
heavily on the fee tier:

{_fmt_table(fee_tbl)}

![Fee sensitivity]({f['fee']})

![Latency sensitivity]({f['latency']})
""" + (f"""
## Market maker up close

![Market maker zoom]({f['mm_zoom']})
""" if "mm_zoom" in f else "")


def main() -> None:
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("--data", required=True)
    p.add_argument("--out", default="results/report")
    p.add_argument("--train-frac", type=float, default=0.5)
    p.add_argument("--maker-bps", type=float, default=1.0)
    p.add_argument("--taker-bps", type=float, default=4.0)
    p.add_argument("--slippage-bps", type=float, default=1.0)
    p.add_argument("--latency-ms", type=float, default=5.0)
    p.add_argument("--max-position", type=float, default=0.2)
    p.add_argument("--quick", action="store_true", help="tiny grids, for CI")
    a = p.parse_args()
    config = dict(maker_bps=a.maker_bps, taker_bps=a.taker_bps, slippage_bps=a.slippage_bps,
                  latency_ms=a.latency_ms, max_position=a.max_position)
    run(a.data, a.out, config, a.train_frac, a.quick)


if __name__ == "__main__":
    main()
