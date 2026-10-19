// pybind11 bindings: exposes the C++ engine to the Python research layer.
//
// Design points:
//  * Trades come in as numpy arrays and are read in place (no copy into a
//    std::vector), via ArraySource below.
//  * The GIL is released for the whole backtest, so Python can run many
//    backtests in parallel threads (parameter sweeps) on real cores.
//  * Results come back as plain dicts of numpy arrays; the Python wrapper
//    turns them into pandas DataFrames.

#include <pybind11/numpy.h>
#include <pybind11/pybind11.h>
#include <pybind11/stl.h>

#include <cstring>
#include <stdexcept>
#include <string>
#include <vector>

#include "engine/data_source.hpp"
#include "engine/engine.hpp"
#include "engine/event_sink.hpp"
#include "engine/strategies/registry.hpp"

namespace py = pybind11;
using namespace te;

namespace {

template <typename T>
using carray = py::array_t<T, py::array::c_style | py::array::forcecast>;

// Streams trades straight out of numpy buffers. The arrays are owned by the
// caller and stay alive for the duration of the (GIL-released) run.
class ArraySource final : public DataSource {
public:
    ArraySource(const std::int64_t* ts, const double* px, const double* qty, const bool* bim, std::size_t n)
        : ts_(ts), px_(px), qty_(qty), bim_(bim), n_(n) {}

    bool next(MarketTrade& out) override {
        if (i_ >= n_) return false;
        out.ts = ts_[i_];
        out.price = px_[i_];
        out.qty = qty_[i_];
        out.buyer_is_maker = bim_[i_];
        out.trade_id = i_;
        ++i_;
        return true;
    }

private:
    const std::int64_t* ts_;
    const double* px_;
    const double* qty_;
    const bool* bim_;
    std::size_t n_;
    std::size_t i_ = 0;
};

// Captures strategy signals (fair value, quotes, momentum score...) for plotting.
class SignalRecorder final : public EventSink {
public:
    std::vector<Timestamp> ts;
    std::vector<std::string> name;
    std::vector<double> value;
    void on_signal(const SignalUpdate& s) override {
        ts.push_back(s.ts);
        name.push_back(s.name);
        value.push_back(s.value);
    }
};

template <typename T>
py::array_t<T> to_numpy(const std::vector<T>& v) {
    py::array_t<T> a(static_cast<py::ssize_t>(v.size()));
    if (!v.empty()) std::memcpy(a.mutable_data(), v.data(), v.size() * sizeof(T));
    return a;
}

py::dict metrics_to_dict(const Metrics& m) {
    py::dict d;
    d["initial_cash"] = m.initial_cash;
    d["final_equity"] = m.final_equity;
    d["total_pnl"] = m.total_pnl;
    d["return_pct"] = m.return_pct;
    d["realized_pnl"] = m.realized_pnl;
    d["fees"] = m.fees;
    d["buy_and_hold_return_pct"] = m.buy_and_hold_return_pct;
    d["sharpe"] = m.sharpe;
    d["max_drawdown"] = m.max_drawdown;
    d["max_drawdown_pct"] = m.max_drawdown_pct;
    d["max_abs_position"] = m.max_abs_position;
    d["final_position"] = m.final_position;
    d["fills"] = m.fills;
    d["maker_fills"] = m.maker_fills;
    d["orders_sent"] = m.orders_sent;
    d["orders_rejected"] = m.orders_rejected;
    d["traded_notional"] = m.traded_notional;
    d["pnl_per_notional_bps"] = m.pnl_per_notional_bps;
    d["market_trades"] = m.market_trades;
    d["events"] = m.events;
    d["duration_days"] = m.duration_days;
    d["wall_seconds"] = m.wall_seconds;
    d["events_per_second"] = m.events_per_second;
    return d;
}

py::dict result_to_dict(const BacktestResult& r, const SignalRecorder* sig) {
    const std::size_t nf = r.fills.size();
    std::vector<std::int64_t> f_ts(nf);
    std::vector<std::uint64_t> f_id(nf);
    std::vector<std::int8_t> f_side(nf);
    std::vector<double> f_px(nf), f_qty(nf), f_fee(nf);
    py::array_t<bool> f_maker(static_cast<py::ssize_t>(nf));
    auto maker_ptr = f_maker.mutable_data();
    for (std::size_t i = 0; i < nf; ++i) {
        const Fill& f = r.fills[i];
        f_ts[i] = f.ts;
        f_id[i] = f.order_id;
        f_side[i] = static_cast<std::int8_t>(sign(f.side));
        f_px[i] = f.price;
        f_qty[i] = f.qty;
        f_fee[i] = f.fee;
        maker_ptr[i] = f.is_maker;
    }
    py::dict fills;
    fills["ts_us"] = to_numpy(f_ts);
    fills["order_id"] = to_numpy(f_id);
    fills["side"] = to_numpy(f_side);
    fills["price"] = to_numpy(f_px);
    fills["qty"] = to_numpy(f_qty);
    fills["fee"] = to_numpy(f_fee);
    fills["is_maker"] = f_maker;

    const std::size_t ne = r.equity_curve.size();
    std::vector<std::int64_t> e_ts(ne);
    std::vector<double> e_eq(ne), e_pos(ne), e_px(ne);
    for (std::size_t i = 0; i < ne; ++i) {
        e_ts[i] = r.equity_curve[i].ts;
        e_eq[i] = r.equity_curve[i].equity;
        e_pos[i] = r.equity_curve[i].position;
        e_px[i] = r.equity_curve[i].mark_price;
    }
    py::dict equity;
    equity["ts_us"] = to_numpy(e_ts);
    equity["equity"] = to_numpy(e_eq);
    equity["position"] = to_numpy(e_pos);
    equity["mark_price"] = to_numpy(e_px);

    py::dict out;
    out["strategy"] = r.strategy;
    out["metrics"] = metrics_to_dict(r.metrics);
    out["fills"] = fills;
    out["equity"] = equity;
    if (sig) {
        py::dict s;
        s["ts_us"] = to_numpy(sig->ts);
        s["name"] = py::cast(sig->name);
        s["value"] = to_numpy(sig->value);
        out["signals"] = s;
    }
    return out;
}

py::dict run_with_source(DataSource& src, const std::string& strategy, const Params& params,
                         const EngineConfig& cfg, bool record_signals) {
    auto strat = make_strategy(strategy, params);  // throws before releasing the GIL
    SignalRecorder recorder;
    BacktestResult result;
    {
        py::gil_scoped_release release;
        Engine engine(cfg);
        if (record_signals) engine.add_sink(&recorder);
        result = engine.run(src, *strat);
    }
    return result_to_dict(result, record_signals ? &recorder : nullptr);
}

}  // namespace

PYBIND11_MODULE(_core, m) {
    m.doc() = "C++ event-driven trading engine (backtest core)";

    py::class_<ExchangeConfig>(m, "ExchangeConfig")
        .def(py::init<>())
        .def_readwrite("order_latency_us", &ExchangeConfig::order_latency_us)
        .def_readwrite("cancel_latency_us", &ExchangeConfig::cancel_latency_us)
        .def_readwrite("maker_fee_bps", &ExchangeConfig::maker_fee_bps)
        .def_readwrite("taker_fee_bps", &ExchangeConfig::taker_fee_bps)
        .def_readwrite("market_slippage_bps", &ExchangeConfig::market_slippage_bps)
        .def_readwrite("touch_fill_ratio", &ExchangeConfig::touch_fill_ratio);

    py::class_<RiskLimits>(m, "RiskLimits")
        .def(py::init<>())
        .def_readwrite("max_position", &RiskLimits::max_position)
        .def_readwrite("max_order_qty", &RiskLimits::max_order_qty)
        .def_readwrite("max_open_orders", &RiskLimits::max_open_orders);

    py::class_<EngineConfig>(m, "EngineConfig")
        .def(py::init<>())
        .def_readwrite("initial_cash", &EngineConfig::initial_cash)
        .def_readwrite("exchange", &EngineConfig::exchange)
        .def_readwrite("risk", &EngineConfig::risk)
        .def_readwrite("equity_sample_us", &EngineConfig::equity_sample_us)
        .def_readwrite("portfolio_publish_us", &EngineConfig::portfolio_publish_us)
        .def_readwrite("record_fills", &EngineConfig::record_fills);

    m.def("strategy_names", &strategy_names, "Names of the built-in strategies.");

    m.def(
        "run_arrays",
        [](carray<std::int64_t> ts, carray<double> price, carray<double> qty, carray<bool> buyer_is_maker,
           const std::string& strategy, const Params& params, const EngineConfig& cfg, bool record_signals) {
            const auto n = static_cast<std::size_t>(ts.size());
            if (static_cast<std::size_t>(price.size()) != n || static_cast<std::size_t>(qty.size()) != n ||
                static_cast<std::size_t>(buyer_is_maker.size()) != n)
                throw std::invalid_argument("ts, price, qty and buyer_is_maker must have the same length");
            ArraySource src(ts.data(), price.data(), qty.data(), buyer_is_maker.data(), n);
            return run_with_source(src, strategy, params, cfg, record_signals);
        },
        py::arg("ts_us"), py::arg("price"), py::arg("qty"), py::arg("buyer_is_maker"), py::arg("strategy"),
        py::arg("params") = Params{}, py::arg("config") = EngineConfig{}, py::arg("record_signals") = false,
        "Run a backtest on trades held in numpy arrays (read in place, GIL released).");

    m.def(
        "run_file",
        [](const std::string& path, const std::string& strategy, const Params& params, const EngineConfig& cfg,
           bool record_signals) {
            CsvTradeSource src(path);
            return run_with_source(src, strategy, params, cfg, record_signals);
        },
        py::arg("path"), py::arg("strategy"), py::arg("params") = Params{}, py::arg("config") = EngineConfig{},
        py::arg("record_signals") = false, "Run a backtest streaming trades from a CSV file.");

    m.def(
        "load_trades",
        [](const std::string& path) {
            std::vector<std::int64_t> ts;
            std::vector<double> px, qty;
            std::vector<std::uint8_t> bim;
            std::vector<std::uint64_t> id;
            std::size_t skipped = 0;
            {
                py::gil_scoped_release release;
                CsvTradeSource src(path);
                MarketTrade t;
                while (src.next(t)) {
                    ts.push_back(t.ts);
                    px.push_back(t.price);
                    qty.push_back(t.qty);
                    bim.push_back(t.buyer_is_maker ? 1 : 0);
                    id.push_back(t.trade_id);
                }
                skipped = src.lines_skipped();
            }
            py::array_t<bool> bim_arr(static_cast<py::ssize_t>(bim.size()));
            if (!bim.empty()) std::memcpy(bim_arr.mutable_data(), bim.data(), bim.size());
            py::dict d;
            d["ts_us"] = to_numpy(ts);
            d["price"] = to_numpy(px);
            d["qty"] = to_numpy(qty);
            d["buyer_is_maker"] = bim_arr;
            d["trade_id"] = to_numpy(id);
            d["lines_skipped"] = skipped;
            return d;
        },
        py::arg("path"), "Parse a trades CSV (Binance or normalized format) with the C++ loader.");
}
