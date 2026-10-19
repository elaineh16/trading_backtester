# Algorithmic Trading System

An event-driven trading system with a **C++ engine** for tick-level market data, strategy
execution and portfolio tracking, plus a **Python research layer** (pybind11) for comparing
strategies on historical crypto trades.

```
 Binance trade dumps ──┐                                     ┌──► research/ (pandas, matplotlib)
 (or a live feed)      │   ┌───────────────────────────┐     │      sweeps · out-of-sample tests
                       ├──►│  C++ event-driven engine  │─────┤      cost sensitivity · notebook
 synthetic tape ───────┘   │  event queue · simulated  │     │
                           │  exchange · portfolio ·   │     └──► te_backtest CLI
                           │  risk · strategies        │
                           └─────────────┬─────────────┘
                                         │ EventSink: orders, fills, signals, portfolio
                                         ▼
                                Redis → WebSocket dashboard   (next milestone)
```

## Highlights

* **Event-driven C++20 engine** ([`engine/`](engine/README.md)). It merges the trade stream with scheduled
  events (order activation after latency, cancels, timers) in strict timestamp order.
  Deterministic, and **~9M trades/s** for the market maker on one core.
* **Simulation details that matter for realistic results.** Order and cancel latency, a queue-position fill model,
  maker/taker fees, slippage and pre-trade risk limits. Unit tests prove that no order fills on the trade
  that triggered it (no look-ahead).
* **Two opposite strategies.** An inventory-skewed market maker (Avellaneda–Stoikov style) and an
  EMA momentum strategy, compared head-to-head on the same tape and the same costs.
* **Overfitting checks.** Parameters are chosen on one time window and judged on a later, untouched one.
  The report shows how well in-sample rankings predict out-of-sample ones.
* **Python bindings.** Numpy arrays are read in place, and the GIL is released during a run, so parameter
  sweeps run in parallel threads (~350 backtests over 2M trades in ~15 s on 2 cores).
* **Tested and automated.** 31 GoogleTest + 15 pytest cases, and CI that builds, tests,
  benchmarks and runs a smoke report.

## Quick start

```bash
pip install -r requirements-research.txt
cmake -S engine -B engine/build -G Ninja -DCMAKE_BUILD_TYPE=Release -DTE_BUILD_PYTHON=ON
cmake --build engine/build && ctest --test-dir engine/build

# Real data: Binance public archive, no API key (BTCUSDT is ~1-3M trades/day)
python tools/download_binance_trades.py --symbol BTCUSDT --start 2026-10-01 --end 2026-10-07

# Full study: sweeps, out-of-sample comparison, cost sensitivity -> results/report/REPORT.md
PYTHONPATH=engine/python python -m research.report --data data/trades/BTCUSDT-trades-2026-10-01.csv

# Single backtest from the CLI
./engine/build/te_backtest --data data/trades/BTCUSDT-trades-2026-10-01.csv --strategy market_maker --out results/mm
```

No data yet? `python tools/generate_synthetic_trades.py --trades 2000000` makes a synthetic tape that
exercises the whole pipeline. It is not market data, so don't read anything into its results.

From Python:

```python
import trading_engine as te
trades = te.load_trades("data/trades/BTCUSDT-trades-2026-10-01.csv")   # C++ parser
res = te.backtest(trades, "market_maker", {"base_half_spread_bps": 3}, maker_bps=0.0, latency_ms=10)
res.metrics["total_pnl"], res.fills, res.equity
```

## Research

[`notebooks/02_strategy_comparison.ipynb`](notebooks/02_strategy_comparison.ipynb) walks through the
whole study: what the market maker's quotes look like up close, parameter heatmaps, the
in-sample vs out-of-sample check, the head-to-head comparison, and how results depend on fees and latency.
It uses the first Binance file found in `data/trades/` and falls back to the synthetic tape.

## Repository layout

```
engine/                C++ engine (CMake): include/, src/, apps/te_backtest, bench/, tests/ (GoogleTest)
engine/python/         trading_engine Python package (pybind11 bindings; `pip install ./engine`)
research/              experiments.py (sweeps, train/test), plotting.py, report.py (CLI)
tools/                 Binance trade downloader, synthetic tape generator
notebooks/             02_strategy_comparison.ipynb (+ the script that builds it)
tests/                 pytest suite for the bindings and research layer
backtester/, strategies/, analysis/, main.py   legacy daily-bar Python backtester
```

## Roadmap

- [x] Event-driven C++ engine, simulated exchange, portfolio, risk limits
- [x] Market-making and momentum strategies, tested against look-ahead
- [x] Python bindings, parameter sweeps, out-of-sample validation, notebook
- [ ] Live Binance WebSocket feed into the same engine
- [ ] Redis pub/sub + cache for signals and portfolio state
- [ ] WebSocket gateway and live dashboard; tick-to-browser latency measurement
- [ ] Docker Compose for the full stack

## Legacy daily-bar backtester

The original Python backtester (moving-average, RSI and MACD strategies on daily OHLCV CSVs)
still works:

```bash
pip install -r requirements.txt
python main.py --list-strategies
python main.py --strategy moving_average --data data/TSLA.csv
```

## Disclaimer

For educational and research purposes only. Backtest results do not guarantee future performance.
