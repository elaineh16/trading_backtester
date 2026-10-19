#pragma once

#include "engine/strategy.hpp"

namespace te {

// Inventory-aware market maker, in the spirit of Avellaneda & Stoikov (2008):
//
//   fair      = EWMA of trade prices
//   sigma     = EWMA std-dev of fair-value log returns (bps)
//   reserve   = fair * (1 - skew_bps * q / q_max)      <- leans quotes against inventory
//   half_sprd = base_half_spread_bps + vol_multiplier * sigma
//   bid/ask   = reserve * (1 -/+ half_sprd)
//
// When long, both quotes shift down: the ask becomes more aggressive (to sell
// inventory off) and the bid less aggressive. It stops quoting the side that
// would push |inventory| past max_inventory.
//
// Params (defaults in brackets): order_qty [0.01], base_half_spread_bps [2],
// vol_multiplier [1], inventory_skew_bps [3], max_inventory [0.1],
// fair_alpha [0.05], vol_alpha [0.01], requote_bps [0.5],
// min_requote_ms [100], tick_size [0.01], warmup_trades [200]
class MarketMaker : public Strategy {
public:
    explicit MarketMaker(const Params& p = {});

    std::string name() const override { return "market_maker"; }
    void on_trade(const MarketTrade& trade, Context& ctx) override;
    void on_fill(const Fill& fill, Context& ctx) override;
    void on_end(Context& ctx) override { ctx.cancel_all(); }

    double fair_value() const { return fair_; }

private:
    struct Quote {
        OrderId id = 0;
        double price = 0.0;
        double remaining = 0.0;
    };
    void requote(Context& ctx);
    void manage_side(Context& ctx, Side side, Quote& q, double desired_px, bool allowed);

    // params
    double order_qty_, base_half_spread_bps_, vol_multiplier_, skew_bps_, max_inventory_;
    double fair_alpha_, vol_alpha_, requote_bps_, tick_size_;
    Timestamp min_requote_us_;
    std::size_t warmup_trades_;

    // state
    double fair_ = 0.0;
    double var_bps2_ = 0.0;
    std::size_t seen_ = 0;
    Timestamp last_requote_ = 0;
    Quote bid_, ask_;
};

}  // namespace te
