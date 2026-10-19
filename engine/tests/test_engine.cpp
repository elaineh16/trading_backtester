#include <gtest/gtest.h>

#include <functional>

#include "engine/engine.hpp"
#include "engine/strategies/registry.hpp"
#include "helpers.hpp"

using namespace te;
using te::test::ms;
using te::test::trade;

namespace {

// Strategy whose behaviour is defined inline by each test.
class Scripted : public Strategy {
public:
    std::function<void(const MarketTrade&, Context&, int)> on_trade_fn;
    std::function<void(std::uint64_t, Context&)> on_timer_fn;
    std::vector<Fill> fills;
    int n = 0;

    std::string name() const override { return "scripted"; }
    void on_trade(const MarketTrade& t, Context& c) override {
        if (on_trade_fn) on_trade_fn(t, c, n);
        ++n;
    }
    void on_fill(const Fill& f, Context&) override { fills.push_back(f); }
    void on_timer(std::uint64_t id, Context& c) override {
        if (on_timer_fn) on_timer_fn(id, c);
    }
};

EngineConfig cfg() {
    EngineConfig c;
    c.exchange.order_latency_us = ms(10);
    c.exchange.cancel_latency_us = ms(10);
    c.exchange.market_slippage_bps = 0.0;
    c.exchange.taker_fee_bps = 0.0;
    c.exchange.maker_fee_bps = 0.0;
    c.risk.max_position = 5.0;
    c.risk.max_order_qty = 5.0;
    return c;
}

struct RecordingSink : EventSink {
    std::vector<SignalUpdate> signals;
    std::size_t orders = 0, fills = 0, portfolio_updates = 0;
    void on_order(const Order&) override { ++orders; }
    void on_fill(const Fill&, const Portfolio&) override { ++fills; }
    void on_signal(const SignalUpdate& s) override { signals.push_back(s); }
    void on_portfolio(Timestamp, const Portfolio&) override { ++portfolio_updates; }
};

}  // namespace

// The core anti-look-ahead guarantee: an order sent in reaction to a print
// can never fill at that print's price, nor on any print inside the latency window.
TEST(Engine, MarketOrderFillsOnFirstPrintAfterLatencyOnly) {
    Scripted s;
    s.on_trade_fn = [](const MarketTrade&, Context& c, int i) {
        if (i == 0) c.submit_market(Side::Buy, 1.0);
    };
    VectorSource src({trade(0, 100.0), trade(ms(5), 101.0), trade(ms(20), 102.0), trade(ms(30), 103.0)});
    Engine e(cfg());
    auto r = e.run(src, s);
    ASSERT_EQ(s.fills.size(), 1u);
    EXPECT_DOUBLE_EQ(s.fills[0].price, 102.0);
    EXPECT_EQ(s.fills[0].ts, ms(20));
    EXPECT_DOUBLE_EQ(r.metrics.final_position, 1.0);
}

TEST(Engine, LimitOrderDoesNotFillOnTriggeringPrint) {
    Scripted s;
    s.on_trade_fn = [](const MarketTrade& t, Context& c, int i) {
        if (i == 0) c.submit_limit(Side::Buy, t.price, 1.0);  // join at the print price
    };
    // Print 2 is inside latency, print 3 trades through.
    VectorSource src({trade(0, 100.0, 1, true), trade(ms(5), 99.0, 1, true), trade(ms(15), 99.5, 1, true)});
    Engine e(cfg());
    e.run(src, s);
    ASSERT_EQ(s.fills.size(), 1u);
    EXPECT_EQ(s.fills[0].ts, ms(15));
    EXPECT_DOUBLE_EQ(s.fills[0].price, 100.0);
}

TEST(Engine, RiskLimitsCountOpenOrders) {
    auto c = cfg();
    c.risk.max_position = 1.0;
    c.risk.max_order_qty = 1.0;
    Scripted s;
    std::vector<OrderId> ids;
    s.on_trade_fn = [&](const MarketTrade&, Context& ctx, int i) {
        if (i == 0) {
            ids.push_back(ctx.submit_limit(Side::Buy, 90.0, 0.6));
            ids.push_back(ctx.submit_limit(Side::Buy, 89.0, 0.6));  // 1.2 worst case -> reject
            ids.push_back(ctx.submit_limit(Side::Sell, 110.0, 1.0)); // other side is fine
            ids.push_back(ctx.submit_limit(Side::Sell, 111.0, 2.0)); // > max_order_qty
        }
    };
    VectorSource src({trade(0, 100.0), trade(ms(50), 100.0)});
    Engine e(c);
    auto r = e.run(src, s);
    EXPECT_NE(ids[0], 0u);
    EXPECT_EQ(ids[1], 0u);
    EXPECT_NE(ids[2], 0u);
    EXPECT_EQ(ids[3], 0u);
    EXPECT_EQ(r.metrics.orders_rejected, 2u);
}

TEST(Engine, CancelTakesEffectAfterLatency) {
    Scripted s;
    OrderId id = 0;
    s.on_trade_fn = [&](const MarketTrade&, Context& c, int i) {
        if (i == 0) id = c.submit_limit(Side::Buy, 100.0, 1.0);
        if (i == 1) c.cancel(id);  // at 20ms -> lands at 30ms
    };
    VectorSource src({trade(0, 101.0), trade(ms(20), 101.0), trade(ms(25), 99.0, 1, true), trade(ms(40), 98.0, 1, true)});
    Engine e(cfg());
    e.run(src, s);
    ASSERT_EQ(s.fills.size(), 1u);  // filled at 25ms (cancel in flight), not again at 40ms
    EXPECT_EQ(s.fills[0].ts, ms(25));
}

TEST(Engine, TimersFireInOrderBetweenTrades) {
    Scripted s;
    std::vector<std::pair<std::uint64_t, Timestamp>> fired;
    s.on_trade_fn = [](const MarketTrade&, Context& c, int i) {
        if (i == 0) {
            c.schedule_timer(ms(15), 2);
            c.schedule_timer(ms(5), 1);
        }
    };
    s.on_timer_fn = [&](std::uint64_t id, Context& c) { fired.push_back({id, c.now()}); };
    VectorSource src({trade(0, 100.0), trade(ms(10), 100.0), trade(ms(20), 100.0)});
    Engine e(cfg());
    e.run(src, s);
    ASSERT_EQ(fired.size(), 2u);
    EXPECT_EQ(fired[0], std::make_pair(std::uint64_t{1}, ms(5)));
    EXPECT_EQ(fired[1], std::make_pair(std::uint64_t{2}, ms(15)));
}

TEST(Engine, SinksReceiveOrdersFillsSignalsAndPortfolio) {
    Scripted s;
    s.on_trade_fn = [](const MarketTrade&, Context& c, int i) {
        if (i == 0) c.submit_market(Side::Buy, 1.0);
        c.emit_signal("x", i);
    };
    RecordingSink sink;
    VectorSource src({trade(0, 100.0), trade(ms(20), 100.0), trade(ms(2000), 100.0)});
    Engine e(cfg());
    e.add_sink(&sink);
    e.run(src, s);
    EXPECT_EQ(sink.orders, 1u);
    EXPECT_EQ(sink.fills, 1u);
    ASSERT_EQ(sink.signals.size(), 3u);
    EXPECT_EQ(sink.signals[2].strategy, "scripted");
    EXPECT_GE(sink.portfolio_updates, 2u);
}

TEST(Engine, BacktestsAreDeterministic) {
    auto tape = te::test::random_tape(20'000);
    auto run_once = [&] {
        auto strat = make_strategy("market_maker");
        VectorSource src(tape);
        Engine e(cfg());
        return e.run(src, *strat).metrics;
    };
    const auto a = run_once();
    const auto b = run_once();
    EXPECT_EQ(a.fills, b.fills);
    EXPECT_DOUBLE_EQ(a.total_pnl, b.total_pnl);
    EXPECT_GT(a.fills, 0u);
}

TEST(Engine, MetricsMatchPortfolioAccounting) {
    auto tape = te::test::random_tape(20'000);
    auto strat = make_strategy("momentum", {{"bar_seconds", 30}, {"fast_bars", 3}, {"slow_bars", 8}, {"band_bps", 1}});
    VectorSource src(tape);
    auto c = cfg();
    c.exchange.taker_fee_bps = 4.0;
    Engine e(c);
    auto r = e.run(src, *strat);
    double fees = 0;
    for (auto& f : r.fills) fees += f.fee;
    EXPECT_NEAR(r.metrics.fees, fees, 1e-6);
    EXPECT_NEAR(r.metrics.final_equity - r.metrics.initial_cash, r.metrics.total_pnl, 1e-6);
    EXPECT_GT(r.metrics.fills, 0u);
    EXPECT_EQ(r.metrics.market_trades, tape.size());
}
