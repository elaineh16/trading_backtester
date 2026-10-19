#include <gtest/gtest.h>

#include <cmath>

#include "engine/engine.hpp"
#include "engine/strategies/market_maker.hpp"
#include "engine/strategies/momentum.hpp"
#include "engine/strategies/registry.hpp"
#include "helpers.hpp"

using namespace te;

namespace {
EngineConfig cfg() {
    EngineConfig c;
    c.risk.max_position = 1.0;
    c.risk.max_order_qty = 1.0;
    return c;
}
}  // namespace

TEST(MarketMaker, QuotesBothSidesAndRespectsInventoryLimit) {
    auto tape = te::test::random_tape(50'000);
    MarketMaker mm({{"order_qty", 0.01}, {"max_inventory", 0.05}});
    VectorSource src(tape);
    Engine e(cfg());
    auto r = e.run(src, mm);
    EXPECT_GT(r.metrics.fills, 100u);
    EXPECT_EQ(r.metrics.maker_fills, r.metrics.fills);  // only posts passive orders
    // Can exceed by at most one in-flight order per side.
    EXPECT_LE(r.metrics.max_abs_position, 0.05 + 0.02 + 1e-9);
    std::size_t buys = 0, sells = 0;
    for (auto& f : r.fills) (f.side == Side::Buy ? buys : sells)++;
    EXPECT_GT(buys, 10u);
    EXPECT_GT(sells, 10u);
}

TEST(MarketMaker, SkewsQuotesAgainstInventory) {
    // Feed a flat tape; then compare the quotes emitted when flat vs. long.
    struct Capture : EventSink {
        double last_reserve = 0, last_fair = 0;
        void on_signal(const SignalUpdate& s) override {
            if (s.name == "reservation_price") last_reserve = s.value;
            if (s.name == "fair_value") last_fair = s.value;
        }
    };
    // A steady stream of seller-initiated prints far below the bid: the MM keeps getting filled on bids.
    std::vector<MarketTrade> tape;
    for (int i = 0; i < 400; ++i) tape.push_back(te::test::trade(i * 200'000LL, 100.0, 1.0, false));
    for (int i = 400; i < 450; ++i) tape.push_back(te::test::trade(i * 200'000LL, 99.0, 1.0, true));
    MarketMaker mm({{"order_qty", 0.01}, {"max_inventory", 0.05}, {"inventory_skew_bps", 10}, {"fair_alpha", 0.001}});
    Capture cap;
    VectorSource src(tape);
    Engine e(cfg());
    e.add_sink(&cap);
    auto r = e.run(src, mm);
    ASSERT_GT(r.metrics.final_position, 0.0);
    EXPECT_LT(cap.last_reserve, cap.last_fair);  // long inventory -> reservation below fair
}

TEST(Momentum, GoesLongInUptrendAndShortInDowntrend) {
    Params p{{"bar_seconds", 10}, {"fast_bars", 3}, {"slow_bars", 10}, {"band_bps", 2}, {"order_qty", 0.1}};
    {
        Momentum m(p);
        VectorSource src(te::test::random_tape(20'000, 50'000, +0.5));
        Engine e(cfg());
        auto r = e.run(src, m);
        EXPECT_NEAR(r.metrics.final_position, 0.1, 1e-9);
        EXPECT_EQ(r.metrics.maker_fills, 0u);  // all taker
    }
    {
        Momentum m(p);
        VectorSource src(te::test::random_tape(20'000, 50'000, -0.5));
        Engine e(cfg());
        auto r = e.run(src, m);
        EXPECT_NEAR(r.metrics.final_position, -0.1, 1e-9);
    }
}

TEST(Momentum, LongOnlyNeverShorts) {
    Momentum m({{"bar_seconds", 10}, {"fast_bars", 3}, {"slow_bars", 10}, {"band_bps", 2}, {"allow_short", 0}});
    VectorSource src(te::test::random_tape(20'000, 50'000, -0.5));
    Engine e(cfg());
    auto r = e.run(src, m);
    for (const auto& pt : r.equity_curve) EXPECT_GE(pt.position, -1e-12);
}

TEST(Registry, KnownAndUnknownNames) {
    EXPECT_EQ(make_strategy("mm")->name(), "market_maker");
    EXPECT_EQ(make_strategy("momentum")->name(), "momentum");
    EXPECT_THROW(make_strategy("nope"), std::invalid_argument);
}
