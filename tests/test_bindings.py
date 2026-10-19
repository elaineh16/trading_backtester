import json
import shutil
import subprocess
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path

import numpy as np
import pandas as pd
import pytest

import trading_engine as te

ROOT = Path(__file__).resolve().parents[1]


def test_load_trades_matches_pandas(tape_path, trades):
    ref = pd.read_csv(tape_path)
    assert len(trades) == len(ref)
    np.testing.assert_array_equal(trades["ts_us"].to_numpy(), ref["ts_us"].to_numpy())
    np.testing.assert_allclose(trades["price"].to_numpy(), ref["price"].to_numpy())
    np.testing.assert_array_equal(trades["buyer_is_maker"].to_numpy(), ref["buyer_is_maker"].astype(bool).to_numpy())
    assert trades.index.tz is not None and trades.index.is_monotonic_increasing
    assert trades.attrs["lines_skipped"] == 0


def test_dataframe_and_file_inputs_give_identical_results(tape_path, trades):
    a = te.backtest(trades, "market_maker", max_position=0.2)
    b = te.backtest(tape_path, "market_maker", max_position=0.2)
    assert a.metrics["fills"] == b.metrics["fills"] > 0
    assert a.metrics["total_pnl"] == pytest.approx(b.metrics["total_pnl"], abs=1e-9)
    pd.testing.assert_frame_equal(a.fills, b.fills)


def test_result_frames_are_consistent(trades):
    r = te.backtest(trades, "market_maker", max_position=0.2, maker_bps=1.0)
    assert r.fills["fee"].sum() == pytest.approx(r.metrics["fees"], rel=1e-9)
    assert set(r.fills["side"].unique()) <= {-1, 1}
    position = (r.fills["side"] * r.fills["qty"]).sum()
    assert position == pytest.approx(r.metrics["final_position"], abs=1e-9)
    assert r.equity["equity"].iloc[-1] == pytest.approx(r.metrics["final_equity"], rel=1e-12)
    assert (r.equity["mark_price"] > 0).all()
    assert r.summary()["fills"] == r.metrics["fills"]


def test_parallel_runs_match_sequential(trades):
    """The engine releases the GIL; concurrent backtests must not interfere."""
    params = [{"base_half_spread_bps": h} for h in (0.5, 1, 2, 3, 5, 8)]
    seq = [te.backtest(trades, "market_maker", p).metrics["total_pnl"] for p in params]
    with ThreadPoolExecutor(4) as pool:
        par = list(pool.map(lambda p: te.backtest(trades, "market_maker", p).metrics["total_pnl"], params))
    assert seq == par


def test_make_config_maps_friendly_names():
    cfg = te.make_config(cash=5_000, latency_ms=12.5, maker_bps=-0.5, taker_bps=3, max_position=2,
                         max_open_orders=7, sample_sec=30, record_fills=False)
    assert cfg.initial_cash == 5_000
    assert cfg.exchange.order_latency_us == 12_500 == cfg.exchange.cancel_latency_us
    assert cfg.exchange.maker_fee_bps == -0.5
    assert cfg.exchange.taker_fee_bps == 3
    assert cfg.risk.max_position == 2
    assert cfg.risk.max_open_orders == 7
    assert cfg.equity_sample_us == 30_000_000
    assert cfg.record_fills is False
    assert te.config_to_dict(cfg)["latency_ms"] == 12.5


def test_bad_inputs_raise(trades):
    with pytest.raises(TypeError, match="unknown config option"):
        te.make_config(not_a_setting=1)
    with pytest.raises(ValueError, match="unknown strategy"):
        te.backtest(trades, "no_such_strategy")
    with pytest.raises(ValueError, match="missing columns"):
        te.backtest(trades.drop(columns=["qty"]), "momentum")
    with pytest.raises(ValueError, match="same length"):
        te._core.run_arrays(np.zeros(3, np.int64), np.ones(2), np.ones(3), np.zeros(3, bool), "momentum")


def test_signals_recorded_on_request(trades):
    r = te.backtest(trades, "market_maker", record_signals=True)
    assert r.signals is not None
    assert {"fair_value", "bid", "ask"} <= set(r.signals["name"].unique())
    assert te.backtest(trades, "market_maker").signals is None


@pytest.mark.skipif(not (ROOT / "engine/build/te_backtest").exists(), reason="C++ CLI not built")
def test_python_matches_cpp_cli(tmp_path, tape_path, trades):
    out = tmp_path / "cli"
    subprocess.run([str(ROOT / "engine/build/te_backtest"), "--data", str(tape_path), "--strategy", "momentum",
                    "--max-position", "0.2", "--out", str(out)], check=True, capture_output=True)
    cli = json.loads((out / "summary.json").read_text())
    py = te.backtest(trades, "momentum", max_position=0.2).metrics
    for key in ("total_pnl", "fees", "fills", "orders_sent", "max_drawdown"):
        assert py[key] == pytest.approx(cli[key], rel=1e-8, abs=1e-6), key
