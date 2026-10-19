#include "engine/results_io.hpp"

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <stdexcept>

namespace te {

void write_results(const BacktestResult& r, const std::string& dir) {
    std::filesystem::create_directories(dir);

    {
        std::ofstream f(dir + "/summary.json");
        if (!f) throw std::runtime_error("cannot write " + dir + "/summary.json");
        std::string json = r.metrics.to_json();
        // Prepend the strategy name.
        json.insert(2, "  \"strategy\": \"" + r.strategy + "\",\n");
        f << json;
    }
    {
        std::ofstream f(dir + "/fills.csv");
        f << "ts_us,order_id,side,price,qty,fee,is_maker\n";
        char line[160];
        for (const auto& x : r.fills) {
            std::snprintf(line, sizeof(line), "%lld,%llu,%s,%.8f,%.8f,%.8f,%d\n", static_cast<long long>(x.ts),
                          static_cast<unsigned long long>(x.order_id), to_string(x.side), x.price, x.qty, x.fee,
                          x.is_maker ? 1 : 0);
            f << line;
        }
    }
    {
        std::ofstream f(dir + "/equity.csv");
        f << "ts_us,equity,position,mark_price\n";
        char line[128];
        for (const auto& p : r.equity_curve) {
            std::snprintf(line, sizeof(line), "%lld,%.6f,%.8f,%.8f\n", static_cast<long long>(p.ts), p.equity,
                          p.position, p.mark_price);
            f << line;
        }
    }
}

std::string format_summary(const BacktestResult& r) {
    const Metrics& m = r.metrics;
    char buf[2048];
    std::snprintf(buf, sizeof(buf),
                  "Strategy           %s\n"
                  "Market trades      %zu over %.2f days\n"
                  "Throughput         %.2fM events/s (%.3f s wall)\n"
                  "-----------------------------------------------\n"
                  "Total PnL          %.2f  (%.3f%%)\n"
                  "  realized (gross) %.2f\n"
                  "  fees             %.2f\n"
                  "Buy & hold         %.3f%%\n"
                  "Sharpe (ann.)      %.2f\n"
                  "Max drawdown       %.2f  (%.3f%%)\n"
                  "Max |position|     %.4f   final %.4f\n"
                  "Fills              %zu  (maker %zu)\n"
                  "Orders sent        %zu  (rejected %zu)\n"
                  "Traded notional    %.0f   PnL/notional %.2f bps\n",
                  r.strategy.c_str(), m.market_trades, m.duration_days, m.events_per_second / 1e6, m.wall_seconds,
                  m.total_pnl, m.return_pct, m.realized_pnl, m.fees, m.buy_and_hold_return_pct, m.sharpe,
                  m.max_drawdown, m.max_drawdown_pct, m.max_abs_position, m.final_position, m.fills, m.maker_fills,
                  m.orders_sent, m.orders_rejected, m.traded_notional, m.pnl_per_notional_bps);
    return buf;
}

}  // namespace te
