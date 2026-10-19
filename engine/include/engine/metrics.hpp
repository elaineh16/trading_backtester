#pragma once

#include <cstddef>
#include <string>
#include <vector>

#include "engine/portfolio.hpp"

namespace te {

struct Metrics {
    // PnL
    double initial_cash = 0.0;
    double final_equity = 0.0;
    double total_pnl = 0.0;  // after fees
    double return_pct = 0.0;
    double realized_pnl = 0.0;
    double fees = 0.0;
    double buy_and_hold_return_pct = 0.0;  // benchmark: 1 unit notional of initial cash, held

    // Risk
    double sharpe = 0.0;  // annualized (365-day year, crypto trades 24/7), from sampled equity returns
    double max_drawdown = 0.0;
    double max_drawdown_pct = 0.0;
    double max_abs_position = 0.0;
    double final_position = 0.0;

    // Activity
    std::size_t fills = 0;
    std::size_t maker_fills = 0;
    std::size_t orders_sent = 0;
    std::size_t orders_rejected = 0;
    double traded_notional = 0.0;
    double pnl_per_notional_bps = 0.0;

    // Run
    std::size_t market_trades = 0;
    std::size_t events = 0;
    double duration_days = 0.0;
    double wall_seconds = 0.0;
    double events_per_second = 0.0;

    std::string to_json() const;
};

// Fills in the PnL/risk part of Metrics from a finished portfolio.
void compute_portfolio_metrics(const Portfolio& p, const std::vector<Fill>& fills, Timestamp sample_us,
                               double first_price, double last_price, Metrics& m);

}  // namespace te
