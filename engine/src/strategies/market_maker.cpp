#include "engine/strategies/market_maker.hpp"

#include <algorithm>
#include <cmath>

namespace te {

MarketMaker::MarketMaker(const Params& p)
    : order_qty_(param(p, "order_qty", 0.01)),
      base_half_spread_bps_(param(p, "base_half_spread_bps", 2.0)),
      vol_multiplier_(param(p, "vol_multiplier", 1.0)),
      skew_bps_(param(p, "inventory_skew_bps", 3.0)),
      max_inventory_(param(p, "max_inventory", 0.1)),
      fair_alpha_(param(p, "fair_alpha", 0.05)),
      vol_alpha_(param(p, "vol_alpha", 0.01)),
      requote_bps_(param(p, "requote_bps", 0.5)),
      tick_size_(param(p, "tick_size", 0.01)),
      min_requote_us_(static_cast<Timestamp>(param(p, "min_requote_ms", 100.0) * 1000.0)),
      warmup_trades_(static_cast<std::size_t>(param(p, "warmup_trades", 200.0))) {}

void MarketMaker::on_trade(const MarketTrade& trade, Context& ctx) {
    if (seen_++ == 0) {
        fair_ = trade.price;
        return;
    }
    const double prev = fair_;
    fair_ += fair_alpha_ * (trade.price - fair_);
    const double ret_bps = 1e4 * std::log(fair_ / prev);
    var_bps2_ += vol_alpha_ * (ret_bps * ret_bps - var_bps2_);

    if (seen_ < warmup_trades_) return;
    if (ctx.now() - last_requote_ < min_requote_us_) return;
    requote(ctx);
}

void MarketMaker::requote(Context& ctx) {
    last_requote_ = ctx.now();
    const double q = ctx.position();
    const double inv_ratio = max_inventory_ > 0 ? std::clamp(q / max_inventory_, -1.0, 1.0) : 0.0;
    const double sigma_bps = std::sqrt(std::max(var_bps2_, 0.0));
    const double half_bps = base_half_spread_bps_ + vol_multiplier_ * sigma_bps;
    const double reserve = fair_ * (1.0 - skew_bps_ * 1e-4 * inv_ratio);

    double bid_px = std::floor(reserve * (1.0 - half_bps * 1e-4) / tick_size_) * tick_size_;
    double ask_px = std::ceil(reserve * (1.0 + half_bps * 1e-4) / tick_size_) * tick_size_;
    if (ask_px <= bid_px) ask_px = bid_px + tick_size_;

    manage_side(ctx, Side::Buy, bid_, bid_px, q + order_qty_ <= max_inventory_ + 1e-12);
    manage_side(ctx, Side::Sell, ask_, ask_px, q - order_qty_ >= -max_inventory_ - 1e-12);

    ctx.emit_signal("fair_value", fair_);
    ctx.emit_signal("reservation_price", reserve);
    ctx.emit_signal("half_spread_bps", half_bps);
    ctx.emit_signal("bid", bid_.id ? bid_.price : 0.0);
    ctx.emit_signal("ask", ask_.id ? ask_.price : 0.0);
}

void MarketMaker::manage_side(Context& ctx, Side side, Quote& q, double desired_px, bool allowed) {
    if (q.id != 0) {
        const bool stale = std::fabs(q.price - desired_px) / desired_px * 1e4 > requote_bps_;
        if (allowed && !stale) return;  // existing quote is still good
        ctx.cancel(q.id);
        q = Quote{};
    }
    if (!allowed) return;
    const OrderId id = ctx.submit_limit(side, desired_px, order_qty_);
    if (id != 0) q = Quote{id, desired_px, order_qty_};
}

void MarketMaker::on_fill(const Fill& fill, Context& ctx) {
    for (Quote* q : {&bid_, &ask_}) {
        if (q->id == fill.order_id) {
            q->remaining -= fill.qty;
            if (q->remaining <= 1e-12) *q = Quote{};
        }
    }
    // Inventory changed: re-skew immediately rather than waiting for the throttle.
    if (seen_ >= warmup_trades_) requote(ctx);
}

}  // namespace te
