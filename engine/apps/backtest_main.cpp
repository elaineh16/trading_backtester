// Command-line backtester.
//
//   te_backtest --data data/trades/BTCUSDT-trades-2026-10-01.csv --strategy market_maker
//               --param base_half_spread_bps=3 --param order_qty=0.01 --out results/mm
//
// Run with --help for all options.

#include <cstdlib>
#include <iostream>
#include <string>

#include "engine/data_source.hpp"
#include "engine/engine.hpp"
#include "engine/results_io.hpp"
#include "engine/strategies/registry.hpp"

namespace {

void usage() {
    std::cout << R"(Usage: te_backtest --data FILE --strategy NAME [options]

Strategies: market_maker (mm), momentum (mom)

Options:
  --param KEY=VALUE      strategy parameter (repeatable)
  --out DIR              write summary.json, fills.csv, equity.csv
  --cash X               initial cash (default 100000)
  --max-position X       risk limit on |position| incl. open orders (default 1.0)
  --max-order-qty X      risk limit per order (default 1.0)
  --latency-ms X         order + cancel latency (default 5)
  --maker-bps X          maker fee, negative = rebate (default 1.0)
  --taker-bps X          taker fee (default 4.0)
  --slippage-bps X       market-order slippage (default 1.0)
  --touch-ratio X        share of an at-price print we get filled on (default 0.25)
  --sample-sec X         equity-curve sample interval (default 60)
)";
}

}  // namespace

int main(int argc, char** argv) {
    std::string data, strategy_name, out;
    te::Params params;
    te::EngineConfig cfg;

    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        auto val = [&]() -> std::string {
            if (i + 1 >= argc) {
                std::cerr << "missing value for " << a << "\n";
                std::exit(2);
            }
            return argv[++i];
        };
        if (a == "--help" || a == "-h") {
            usage();
            return 0;
        } else if (a == "--data") {
            data = val();
        } else if (a == "--strategy") {
            strategy_name = val();
        } else if (a == "--out") {
            out = val();
        } else if (a == "--param") {
            const std::string kv = val();
            const auto eq = kv.find('=');
            if (eq == std::string::npos) {
                std::cerr << "--param expects KEY=VALUE, got " << kv << "\n";
                return 2;
            }
            params[kv.substr(0, eq)] = std::stod(kv.substr(eq + 1));
        } else if (a == "--cash") {
            cfg.initial_cash = std::stod(val());
        } else if (a == "--max-position") {
            cfg.risk.max_position = std::stod(val());
        } else if (a == "--max-order-qty") {
            cfg.risk.max_order_qty = std::stod(val());
        } else if (a == "--latency-ms") {
            const auto us = static_cast<te::Timestamp>(std::stod(val()) * 1000);
            cfg.exchange.order_latency_us = us;
            cfg.exchange.cancel_latency_us = us;
        } else if (a == "--maker-bps") {
            cfg.exchange.maker_fee_bps = std::stod(val());
        } else if (a == "--taker-bps") {
            cfg.exchange.taker_fee_bps = std::stod(val());
        } else if (a == "--slippage-bps") {
            cfg.exchange.market_slippage_bps = std::stod(val());
        } else if (a == "--touch-ratio") {
            cfg.exchange.touch_fill_ratio = std::stod(val());
        } else if (a == "--sample-sec") {
            cfg.equity_sample_us = static_cast<te::Timestamp>(std::stod(val()) * te::kMicrosPerSecond);
        } else {
            std::cerr << "unknown option " << a << "\n";
            usage();
            return 2;
        }
    }
    if (data.empty() || strategy_name.empty()) {
        usage();
        return 2;
    }

    try {
        auto strategy = te::make_strategy(strategy_name, params);
        te::CsvTradeSource source(data);
        te::Engine engine(cfg);
        const auto result = engine.run(source, *strategy);
        std::cout << te::format_summary(result);
        if (source.lines_skipped() > 0) std::cerr << "warning: skipped " << source.lines_skipped() << " bad lines\n";
        if (!out.empty()) {
            te::write_results(result, out);
            std::cout << "Results written to " << out << "/\n";
        }
    } catch (const std::exception& e) {
        std::cerr << "error: " << e.what() << "\n";
        return 1;
    }
    return 0;
}
