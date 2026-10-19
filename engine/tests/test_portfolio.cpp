#include <gtest/gtest.h>

#include "engine/portfolio.hpp"

using namespace te;

namespace {
Fill fill(Side side, double px, double qty, double fee = 0.0) {
    Fill f;
    f.side = side;
    f.price = px;
    f.qty = qty;
    f.fee = fee;
    return f;
}
}  // namespace

TEST(Portfolio, AverageCostAndRealizedPnl) {
    Portfolio p(10'000);
    p.apply_fill(fill(Side::Buy, 100, 1));
    p.apply_fill(fill(Side::Buy, 110, 1));
    EXPECT_DOUBLE_EQ(p.position(), 2);
    EXPECT_DOUBLE_EQ(p.avg_entry_price(), 105);

    p.apply_fill(fill(Side::Sell, 120, 1));
    EXPECT_DOUBLE_EQ(p.realized_pnl(), 15);
    EXPECT_DOUBLE_EQ(p.avg_entry_price(), 105);  // unchanged when reducing
}

TEST(Portfolio, FlipLongToShort) {
    Portfolio p(10'000);
    p.apply_fill(fill(Side::Buy, 100, 1));
    p.apply_fill(fill(Side::Sell, 90, 3));  // close 1 at -10, open short 2 @ 90
    EXPECT_DOUBLE_EQ(p.position(), -2);
    EXPECT_DOUBLE_EQ(p.realized_pnl(), -10);
    EXPECT_DOUBLE_EQ(p.avg_entry_price(), 90);

    p.apply_fill(fill(Side::Buy, 80, 2));  // cover short +20
    EXPECT_DOUBLE_EQ(p.position(), 0);
    EXPECT_DOUBLE_EQ(p.realized_pnl(), 10);
}

TEST(Portfolio, EquityIncludesFeesAndMarkToMarket) {
    Portfolio p(1'000);
    p.apply_fill(fill(Side::Buy, 100, 2, 0.5));
    p.mark(0, 105);
    EXPECT_DOUBLE_EQ(p.cash(), 1'000 - 200 - 0.5);
    EXPECT_DOUBLE_EQ(p.equity(), 1'000 + 10 - 0.5);
    EXPECT_DOUBLE_EQ(p.unrealized_pnl(), 10);
    EXPECT_DOUBLE_EQ(p.total_pnl(), 9.5);
    EXPECT_DOUBLE_EQ(p.fees_paid(), 0.5);
}

TEST(Portfolio, AccountingIdentityHolds) {
    // total_pnl == realized + unrealized - fees, through many fills
    Portfolio p(50'000);
    const double px[] = {100, 102, 99, 101, 97, 103, 100};
    const Side sd[] = {Side::Buy, Side::Buy, Side::Sell, Side::Sell, Side::Sell, Side::Buy, Side::Buy};
    const double qty[] = {1, 2, 4, 0.5, 1, 3, 0.5};
    for (int i = 0; i < 7; ++i) {
        p.apply_fill(fill(sd[i], px[i], qty[i], 0.1));
        p.mark(0, px[i] + 0.5);
        EXPECT_NEAR(p.total_pnl(), p.realized_pnl() + p.unrealized_pnl() - p.fees_paid(), 1e-9);
    }
}
