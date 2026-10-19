#include <gtest/gtest.h>

#include <cstdio>
#include <filesystem>
#include <fstream>

#include "engine/data_source.hpp"

using namespace te;

namespace {
std::string write_tmp(const std::string& name, const std::string& body) {
    auto path = std::filesystem::temp_directory_path() / name;
    std::ofstream(path) << body;
    return path.string();
}
}  // namespace

TEST(DataSource, NormalizesTimestampResolution) {
    EXPECT_EQ(normalize_timestamp(1'700'000'000LL), 1'700'000'000'000'000LL);              // s
    EXPECT_EQ(normalize_timestamp(1'700'000'000'123LL), 1'700'000'000'123'000LL);          // ms
    EXPECT_EQ(normalize_timestamp(1'700'000'000'123'456LL), 1'700'000'000'123'456LL);      // us
    EXPECT_EQ(normalize_timestamp(1'700'000'000'123'456'789LL), 1'700'000'000'123'456LL);  // ns
}

TEST(DataSource, ParsesBinanceSpotTradesWithoutHeader) {
    auto p = write_tmp("te_binance.csv",
                       "100,65000.10,0.002,130.0002,1759276800123,True,True\n"
                       "101,65000.20,0.010,650.002,1759276800456789,False,True\n");
    CsvTradeSource src(p);
    MarketTrade t;
    ASSERT_TRUE(src.next(t));
    EXPECT_EQ(t.trade_id, 100u);
    EXPECT_DOUBLE_EQ(t.price, 65000.10);
    EXPECT_DOUBLE_EQ(t.qty, 0.002);
    EXPECT_EQ(t.ts, 1759276800123000LL);
    EXPECT_TRUE(t.buyer_is_maker);
    EXPECT_EQ(t.aggressor(), Side::Sell);
    ASSERT_TRUE(src.next(t));
    EXPECT_EQ(t.ts, 1759276800456789LL);  // microsecond file
    EXPECT_FALSE(t.buyer_is_maker);
    EXPECT_FALSE(src.next(t));
}

TEST(DataSource, ParsesNormalizedFormatWithHeaderAndSkipsBadLines) {
    auto p = write_tmp("te_norm.csv",
                       "ts_us,price,qty,buyer_is_maker,trade_id\n"
                       "1700000000000000,100.5,1.5,1,7\n"
                       "garbage line\n"
                       "1700000000000100,100.6,0.5,0,8\n");
    CsvTradeSource src(p);
    MarketTrade t;
    ASSERT_TRUE(src.next(t));
    EXPECT_DOUBLE_EQ(t.price, 100.5);
    EXPECT_TRUE(t.buyer_is_maker);
    EXPECT_EQ(t.trade_id, 7u);
    ASSERT_TRUE(src.next(t));
    EXPECT_EQ(t.trade_id, 8u);
    EXPECT_FALSE(src.next(t));
    EXPECT_EQ(src.lines_skipped(), 1u);
}

TEST(DataSource, MissingFileThrows) { EXPECT_THROW(CsvTradeSource("/nonexistent/file.csv"), std::runtime_error); }
