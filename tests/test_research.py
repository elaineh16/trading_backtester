import json

import pytest

from research import compare, config_sweep, param_grid, run_grid, select_and_validate, time_split
from research import report


def test_param_grid_is_cartesian():
    g = param_grid({"a": [1, 2], "b": [3, 4, 5]})
    assert len(g) == 6
    assert {"a": 2, "b": 5} in g


def test_time_split_is_chronological(trades):
    train, test = time_split(trades, 0.6)
    assert len(train) + len(test) == len(trades)
    assert train.index[-1] <= test.index[0]
    with pytest.raises(ValueError):
        time_split(trades, 1.5)


def test_run_grid_returns_one_row_per_combo(trades):
    df = run_grid(trades, "momentum", {"bar_seconds": [15, 60], "band_bps": [2, 5]}, base_params={"order_qty": 0.1})
    assert len(df) == 4
    assert {"bar_seconds", "band_bps", "total_pnl", "fills"} <= set(df.columns)


def test_select_and_validate_picks_in_sample_best(trades):
    grid = {"base_half_spread_bps": [1, 3, 8]}
    split = select_and_validate(trades, "market_maker", grid, min_fills=1)
    eligible = split.in_sample[split.in_sample["fills"] >= 1]
    best = eligible.sort_values("total_pnl").iloc[-1]
    assert split.best_params == {"base_half_spread_bps": best["base_half_spread_bps"]}
    assert -1.0 <= split.rank_correlation() <= 1.0
    assert 0 < split.oos_percentile_of_best() <= 100
    assert split.train_period[1] <= split.test_period[0]


def test_higher_maker_fee_strictly_lowers_mm_pnl(trades):
    df = config_sweep(trades, "market_maker", {}, "maker_bps", [-1.0, 0.0, 1.0, 2.0])
    assert df["fills"].nunique() == 1  # fees don't change behaviour, only PnL
    assert df["total_pnl"].is_monotonic_decreasing


def test_compare_labels_rows(trades):
    table, results = compare(trades, [("mm", "market_maker", {}), ("mom", "momentum", {})])
    assert list(table.index) == ["mm", "mom"]
    assert results["mom"].strategy == "momentum"


def test_report_quick_run_writes_everything(tape_path, tmp_path):
    out = tmp_path / "report"
    summary = report.run(str(tape_path), str(out), {"max_position": 0.2}, quick=True)
    for name in summary["figures"].values():
        assert (out / name).stat().st_size > 10_000
    md = (out / "REPORT.md").read_text()
    assert "synthetic tape" in md and "Out-of-sample results" in md
    saved = json.loads((out / "results.json").read_text())
    assert set(saved["test_metrics"]) == {"market_maker", "momentum"}
