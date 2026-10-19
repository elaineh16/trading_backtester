#pragma once

#include "engine/strategy.hpp"

namespace te {

// Trend-following momentum on time bars built from the trade tape.
//
// Every `bar_seconds`, the bar close updates a fast and a slow EMA.
//   score = (ema_fast / ema_slow - 1) in bps
//   score >  band_bps -> target long  order_qty
//   score < -band_bps -> target short order_qty (or flat if allow_short = 0)
//   otherwise         -> keep the current target (hysteresis band avoids churn)
// Trades to target with market orders, so it pays the spread + taker fee:
// the opposite cost profile to the market maker.
//
// Params: bar_seconds [60], fast_bars [10], slow_bars [30], band_bps [5],
// order_qty [0.05], allow_short [1]
class Momentum : public Strategy {
public:
    explicit Momentum(const Params& p = {});

    std::string name() const override { return "momentum"; }
    void on_trade(const MarketTrade& trade, Context& ctx) override;
    void on_fill(const Fill& fill, Context& ctx) override;

    double target() const { return target_; }

private:
    void on_bar_close(double close, Context& ctx);

    Timestamp bar_us_;
    double fast_alpha_, slow_alpha_, band_bps_, order_qty_;
    bool allow_short_;
    std::size_t slow_bars_;

    Timestamp bar_end_ = 0;
    double last_price_ = 0.0;
    double ema_fast_ = 0.0, ema_slow_ = 0.0;
    std::size_t bars_ = 0;
    double target_ = 0.0;
    double pending_qty_ = 0.0;  // signed quantity of market orders sent but not yet filled
};

}  // namespace te
