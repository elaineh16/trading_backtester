#include "engine/metrics.hpp"

#include <cmath>
#include <cstdio>
#include <sstream>

namespace te {

void compute_portfolio_metrics(const Portfolio& p, const std::vector<Fill>& fills, Timestamp sample_us,
                               double first_price, double last_price, Metrics& m) {
    m.initial_cash = p.initial_cash();
    m.final_equity = p.equity();
    m.total_pnl = p.total_pnl();
    m.return_pct = 100.0 * m.total_pnl / m.initial_cash;
    m.realized_pnl = p.realized_pnl();
    m.fees = p.fees_paid();
    m.max_abs_position = p.max_abs_position();
    m.final_position = p.position();
    m.fills = p.fill_count();
    m.traded_notional = p.traded_notional();
    m.pnl_per_notional_bps = m.traded_notional > 0 ? 1e4 * m.total_pnl / m.traded_notional : 0.0;
    m.buy_and_hold_return_pct = first_price > 0 ? 100.0 * (last_price / first_price - 1.0) : 0.0;

    m.maker_fills = 0;
    for (const auto& f : fills)
        if (f.is_maker) ++m.maker_fills;

    // Sharpe and drawdown from the sampled equity curve.
    const auto& curve = p.equity_curve();
    if (curve.size() >= 3) {
        double sum = 0.0, sum_sq = 0.0;
        std::size_t n = 0;
        for (std::size_t i = 1; i < curve.size(); ++i) {
            if (curve[i - 1].equity <= 0) continue;
            const double r = curve[i].equity / curve[i - 1].equity - 1.0;
            sum += r;
            sum_sq += r * r;
            ++n;
        }
        if (n >= 2) {
            const double mean = sum / n;
            const double var = (sum_sq - n * mean * mean) / (n - 1);
            const double periods_per_year = 365.0 * 86400.0 * kMicrosPerSecond / static_cast<double>(sample_us);
            m.sharpe = var > 0 ? mean / std::sqrt(var) * std::sqrt(periods_per_year) : 0.0;
        }
    }
    double peak = m.initial_cash;
    for (const auto& pt : curve) {
        peak = std::max(peak, pt.equity);
        const double dd = peak - pt.equity;
        if (dd > m.max_drawdown) {
            m.max_drawdown = dd;
            m.max_drawdown_pct = 100.0 * dd / peak;
        }
    }
}

std::string Metrics::to_json() const {
    std::ostringstream o;
    char buf[64];
    auto num = [&](const char* key, double v, bool last = false) {
        std::snprintf(buf, sizeof(buf), "%.10g", std::isfinite(v) ? v : 0.0);
        o << "  \"" << key << "\": " << buf << (last ? "\n" : ",\n");
    };
    o << "{\n";
    num("initial_cash", initial_cash);
    num("final_equity", final_equity);
    num("total_pnl", total_pnl);
    num("return_pct", return_pct);
    num("realized_pnl", realized_pnl);
    num("fees", fees);
    num("buy_and_hold_return_pct", buy_and_hold_return_pct);
    num("sharpe", sharpe);
    num("max_drawdown", max_drawdown);
    num("max_drawdown_pct", max_drawdown_pct);
    num("max_abs_position", max_abs_position);
    num("final_position", final_position);
    num("fills", static_cast<double>(fills));
    num("maker_fills", static_cast<double>(maker_fills));
    num("orders_sent", static_cast<double>(orders_sent));
    num("orders_rejected", static_cast<double>(orders_rejected));
    num("traded_notional", traded_notional);
    num("pnl_per_notional_bps", pnl_per_notional_bps);
    num("market_trades", static_cast<double>(market_trades));
    num("events", static_cast<double>(events));
    num("duration_days", duration_days);
    num("wall_seconds", wall_seconds);
    num("events_per_second", events_per_second, true);
    o << "}\n";
    return o.str();
}

}  // namespace te
