#include "engine/strategies/momentum.hpp"

#include <cmath>

namespace te {

Momentum::Momentum(const Params& p)
    : bar_us_(static_cast<Timestamp>(param(p, "bar_seconds", 60.0) * kMicrosPerSecond)),
      fast_alpha_(2.0 / (param(p, "fast_bars", 10.0) + 1.0)),
      slow_alpha_(2.0 / (param(p, "slow_bars", 30.0) + 1.0)),
      band_bps_(param(p, "band_bps", 5.0)),
      order_qty_(param(p, "order_qty", 0.05)),
      allow_short_(param(p, "allow_short", 1.0) != 0.0),
      slow_bars_(static_cast<std::size_t>(param(p, "slow_bars", 30.0))) {}

void Momentum::on_trade(const MarketTrade& trade, Context& ctx) {
    if (bar_end_ == 0) bar_end_ = (trade.ts / bar_us_ + 1) * bar_us_;  // align bars to the clock
    // A print at/after the boundary closes the previous bar with the last price
    // seen *before* it, so the signal never uses the closing print's own price.
    while (trade.ts >= bar_end_) {
        if (last_price_ > 0) on_bar_close(last_price_, ctx);
        bar_end_ += bar_us_;
    }
    last_price_ = trade.price;
}

void Momentum::on_bar_close(double close, Context& ctx) {
    if (bars_++ == 0) {
        ema_fast_ = ema_slow_ = close;
        return;
    }
    ema_fast_ += fast_alpha_ * (close - ema_fast_);
    ema_slow_ += slow_alpha_ * (close - ema_slow_);
    if (bars_ < slow_bars_) return;  // warm-up

    const double score = 1e4 * (ema_fast_ / ema_slow_ - 1.0);
    ctx.emit_signal("momentum_bps", score);

    if (score > band_bps_)
        target_ = order_qty_;
    else if (score < -band_bps_)
        target_ = allow_short_ ? -order_qty_ : 0.0;

    const double delta = target_ - ctx.position() - pending_qty_;
    if (std::fabs(delta) < 1e-9) return;
    const Side side = delta > 0 ? Side::Buy : Side::Sell;
    if (ctx.submit_market(side, std::fabs(delta)) != 0) pending_qty_ += delta;
    ctx.emit_signal("target_position", target_);
}

void Momentum::on_fill(const Fill& fill, Context&) { pending_qty_ -= sign(fill.side) * fill.qty; }

}  // namespace te
