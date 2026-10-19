#pragma once

#include <vector>

#include "engine/types.hpp"

namespace te {

struct EquityPoint {
    Timestamp ts;
    double equity;     // cash + position marked at last trade price
    double position;   // base units
    double mark_price;
};

// Single-instrument position, cash and PnL accounting.
// Realized PnL uses average-cost accounting and handles position flips
// (e.g. long 1 -> sell 3 -> short 2) correctly.
class Portfolio {
public:
    explicit Portfolio(double initial_cash = 100'000.0) : initial_cash_(initial_cash), cash_(initial_cash) {}

    void apply_fill(const Fill& f);
    void mark(Timestamp ts, double price);
    void record_equity(Timestamp ts);

    double initial_cash() const { return initial_cash_; }
    double cash() const { return cash_; }
    double position() const { return position_; }
    double avg_entry_price() const { return avg_price_; }
    double mark_price() const { return mark_price_; }
    double realized_pnl() const { return realized_pnl_; }  // before fees
    double unrealized_pnl() const { return position_ * (mark_price_ - avg_price_); }
    double fees_paid() const { return fees_; }
    double equity() const { return cash_ + position_ * mark_price_; }
    double total_pnl() const { return equity() - initial_cash_; }  // after fees
    double traded_notional() const { return traded_notional_; }
    double max_abs_position() const { return max_abs_position_; }
    std::size_t fill_count() const { return fill_count_; }

    const std::vector<EquityPoint>& equity_curve() const { return curve_; }

private:
    double initial_cash_;
    double cash_;
    double position_ = 0.0;
    double avg_price_ = 0.0;
    double mark_price_ = 0.0;
    double realized_pnl_ = 0.0;
    double fees_ = 0.0;
    double traded_notional_ = 0.0;
    double max_abs_position_ = 0.0;
    std::size_t fill_count_ = 0;
    std::vector<EquityPoint> curve_;
};

}  // namespace te
