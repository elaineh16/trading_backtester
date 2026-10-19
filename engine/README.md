# C++ Event-Driven Trading Engine

Core of the trading system: a deterministic, event-driven engine that replays
tick-level trade data (or, later, a live feed) through a simulated exchange,
runs strategies, and tracks the portfolio.

```
             ┌──────────────┐  next()   ┌───────────────────────────────────────────┐
 trades ───► │  DataSource  │ ────────► │                 Engine                    │
 (CSV/live)  └──────────────┘           │  merge by timestamp:                      │
                                        │   market trades  +  EventQueue            │
                                        │                     (order activations,   │
                                        │                      cancels, timers)     │
                                        │                                           │
                                        │  per trade:                               │
                                        │   1. SimExchange matches LIVE orders      │
                                        │   2. Portfolio applies fills              │
                                        │   3. Strategy.on_trade(...)  ──► orders   │
                                        │      (live only after latency)            │
                                        └───────────────┬───────────────────────────┘
                                                        │ EventSink (orders, fills,
                                                        ▼ signals, portfolio)
                                                  Redis → WebSocket (next phase)
```

## Build & test

```bash
cmake -S engine -B engine/build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build engine/build
./engine/build/te_tests          # GoogleTest suite
./engine/build/te_bench          # throughput benchmark
```

## Run a backtest

```bash
# Real data (Binance public archive, no API key)
python tools/download_binance_trades.py --symbol BTCUSDT --date 2026-10-01
./engine/build/te_backtest --data data/trades/BTCUSDT-trades-2026-10-01.csv \
    --strategy market_maker --max-position 0.2 --out results/mm

# Synthetic tape (offline / CI)
python tools/generate_synthetic_trades.py --trades 1000000
./engine/build/te_backtest --data data/trades/SYNTH-BTCUSDT.csv --strategy momentum --out results/mom
```

Each run writes `summary.json`, `fills.csv` and `equity.csv`. `--help` lists all options.

## Simulation model (what keeps the results honest)

| Concern | How it's handled |
|---|---|
| Look-ahead bias | The exchange matches orders *before* the strategy sees a print; new orders go live only after `order_latency_us`, so they can only fill on later prints. Covered by tests. |
| Latency | Orders and cancels each take a configurable one-way latency. An order can still fill while its cancel is in flight. |
| Queue position | A print exactly at our resting price only fills `touch_fill_ratio` of its size; prints *through* our price fill fully. A single print can't fill more than its own size. |
| Costs | Separate maker / taker fees (bps, rebates allowed) and market-order slippage. |
| Risk | Rejects orders breaching per-order size, max open orders, or worst-case position (position + all open orders on that side). |
| Determinism | Events at equal timestamps run FIFO; identical inputs give identical results (tested). |

Fee defaults (maker 1 bp, taker 4 bp) assume a discounted fee tier. Binance's
base spot tier is much higher, so pass `--maker-bps`/`--taker-bps` to match your
real costs. Fee level alone often decides whether market making is profitable.

## Strategies

* **`market_maker`**: inventory-skewed quoting in the spirit of Avellaneda–Stoikov.
  Fair value is an EWMA of prints. Spread widens with realized volatility. Quotes
  lean against inventory, and it stops quoting the side that would exceed `max_inventory`.
* **`momentum`**: EMA crossover on clock-aligned time bars, with a hysteresis band.
  It trades to target with market orders, so it pays taker costs: the opposite profile to the market maker.

Parameters are passed as `--param key=value`; see each strategy's header for the full list.

## Performance

`te_bench` on a 5M-trade synthetic tape (Release build, single thread, 2-vCPU cloud VM):

| Strategy | Wall time | Trades/s | Engine events/s |
|---|---|---|---|
| market_maker (1.3M orders, 690k fills) | 0.56 s | 8.9M | 12.7M |
| momentum | 0.06 s | 89M | 90M |

## Layout

```
include/engine/   public headers (types, engine, exchange, portfolio, strategies)
src/              implementation
apps/             te_backtest CLI
bench/            throughput benchmark
tests/            GoogleTest suite
```
