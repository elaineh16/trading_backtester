#include <gtest/gtest.h>

#include "engine/sim_exchange.hpp"
#include "helpers.hpp"

using namespace te;
using te::test::trade;

namespace {

ExchangeConfig cfg() {
    ExchangeConfig c;
    c.order_latency_us = 1000;
    c.cancel_latency_us = 1000;
    c.maker_fee_bps = 1.0;
    c.taker_fee_bps = 5.0;
    c.market_slippage_bps = 2.0;
    c.touch_fill_ratio = 0.5;
    return c;
}

Order limit(OrderId id, Side side, double px, double qty) {
    Order o;
    o.id = id;
    o.side = side;
    o.type = OrderType::Limit;
    o.price = px;
    o.qty = qty;
    return o;
}

}  // namespace

TEST(SimExchange, PendingOrderCannotFill) {
    SimExchange ex(cfg());
    ex.submit(limit(1, Side::Buy, 100.0, 1.0), 0);
    std::vector<Fill> fills;
    ex.on_trade(trade(500, 99.0, 5.0, true), fills);  // trades through, but order not live yet
    EXPECT_TRUE(fills.empty());
}

TEST(SimExchange, BuyLimitFillsWhenTradedThrough) {
    SimExchange ex(cfg());
    ex.submit(limit(1, Side::Buy, 100.0, 1.0), 0);
    ex.activate(1);
    std::vector<Fill> fills;
    ex.on_trade(trade(2000, 99.5, 5.0, true), fills);
    ASSERT_EQ(fills.size(), 1u);
    EXPECT_DOUBLE_EQ(fills[0].price, 100.0);  // maker fills at its own price
    EXPECT_DOUBLE_EQ(fills[0].qty, 1.0);
    EXPECT_TRUE(fills[0].is_maker);
    EXPECT_NEAR(fills[0].fee, 100.0 * 1.0 * 1e-4, 1e-12);
    EXPECT_EQ(ex.find(1)->status, OrderStatus::Filled);
}

TEST(SimExchange, BuyLimitIgnoresPrintsAbovePriceAndBuyerInitiatedTouch) {
    SimExchange ex(cfg());
    ex.submit(limit(1, Side::Buy, 100.0, 1.0), 0);
    ex.activate(1);
    std::vector<Fill> fills;
    ex.on_trade(trade(2000, 100.5, 5.0, true), fills);   // above our bid
    ex.on_trade(trade(3000, 100.0, 5.0, false), fills);  // at our price but buyer lifted an offer
    EXPECT_TRUE(fills.empty());
}

TEST(SimExchange, TouchFillIsPartialQueueModel) {
    SimExchange ex(cfg());
    ex.submit(limit(1, Side::Buy, 100.0, 1.0), 0);
    ex.activate(1);
    std::vector<Fill> fills;
    ex.on_trade(trade(2000, 100.0, 1.0, true), fills);  // seller hits 100 for 1.0 -> we get 50%
    ASSERT_EQ(fills.size(), 1u);
    EXPECT_DOUBLE_EQ(fills[0].qty, 0.5);
    EXPECT_EQ(ex.find(1)->status, OrderStatus::PartiallyFilled);
}

TEST(SimExchange, PrintSizeCapsTotalFills) {
    SimExchange ex(cfg());
    ex.submit(limit(1, Side::Sell, 100.0, 1.0), 0);
    ex.submit(limit(2, Side::Sell, 100.0, 1.0), 0);
    ex.activate(1);
    ex.activate(2);
    std::vector<Fill> fills;
    ex.on_trade(trade(2000, 101.0, 1.2, false), fills);
    double total = 0;
    for (auto& f : fills) total += f.qty;
    EXPECT_NEAR(total, 1.2, 1e-12);
}

TEST(SimExchange, MarketOrderPaysSlippageAndTakerFee) {
    SimExchange ex(cfg());
    Order o;
    o.id = 7;
    o.side = Side::Buy;
    o.type = OrderType::Market;
    o.qty = 2.0;
    ex.submit(o, 0);
    ex.activate(7);
    std::vector<Fill> fills;
    ex.on_trade(trade(2000, 100.0, 0.1), fills);
    ASSERT_EQ(fills.size(), 1u);
    EXPECT_NEAR(fills[0].price, 100.0 * (1 + 2e-4), 1e-9);
    EXPECT_FALSE(fills[0].is_maker);
    EXPECT_NEAR(fills[0].fee, fills[0].price * 2.0 * 5e-4, 1e-9);
}

TEST(SimExchange, OrderCanFillWhileCancelIsInFlight) {
    SimExchange ex(cfg());
    ex.submit(limit(1, Side::Buy, 100.0, 1.0), 0);
    ex.activate(1);
    EXPECT_EQ(ex.request_cancel(1, 2000), 3000);
    std::vector<Fill> fills;
    ex.on_trade(trade(2500, 99.0, 5.0, true), fills);  // before the cancel lands
    EXPECT_EQ(fills.size(), 1u);
    EXPECT_FALSE(ex.apply_cancel(1));  // already filled
}

TEST(SimExchange, CancelledOrderDoesNotFill) {
    SimExchange ex(cfg());
    ex.submit(limit(1, Side::Buy, 100.0, 1.0), 0);
    ex.activate(1);
    EXPECT_TRUE(ex.apply_cancel(1));
    std::vector<Fill> fills;
    ex.on_trade(trade(5000, 99.0, 5.0, true), fills);
    EXPECT_TRUE(fills.empty());
    EXPECT_TRUE(ex.open_orders().empty());
}
