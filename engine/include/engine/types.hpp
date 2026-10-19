#pragma once

#include <cstdint>
#include <string>

namespace te {

// All timestamps are microseconds since the Unix epoch.
using Timestamp = std::int64_t;
using OrderId = std::uint64_t;

constexpr Timestamp kMicrosPerSecond = 1'000'000;

enum class Side : std::uint8_t { Buy, Sell };
enum class OrderType : std::uint8_t { Limit, Market };
enum class OrderStatus : std::uint8_t { PendingNew, Live, PartiallyFilled, Filled, Cancelled, Rejected };

inline int sign(Side s) { return s == Side::Buy ? 1 : -1; }
inline const char* to_string(Side s) { return s == Side::Buy ? "BUY" : "SELL"; }

// A public trade print from the exchange tape.
// buyer_is_maker == true means the aggressor was a SELLER (it hit the bid).
struct MarketTrade {
    Timestamp ts = 0;
    double price = 0.0;
    double qty = 0.0;
    bool buyer_is_maker = false;
    std::uint64_t trade_id = 0;

    Side aggressor() const { return buyer_is_maker ? Side::Sell : Side::Buy; }
};

struct Order {
    OrderId id = 0;
    Side side = Side::Buy;
    OrderType type = OrderType::Limit;
    double price = 0.0;  // ignored for market orders
    double qty = 0.0;
    double filled = 0.0;
    Timestamp submit_ts = 0;  // when the strategy sent it
    Timestamp active_ts = 0;  // when the exchange accepts it (submit_ts + latency)
    OrderStatus status = OrderStatus::PendingNew;

    double remaining() const { return qty - filled; }
    bool is_open() const {
        return status == OrderStatus::PendingNew || status == OrderStatus::Live ||
               status == OrderStatus::PartiallyFilled;
    }
};

struct Fill {
    OrderId order_id = 0;
    Timestamp ts = 0;
    Side side = Side::Buy;
    double price = 0.0;
    double qty = 0.0;
    double fee = 0.0;  // in quote currency; negative = rebate
    bool is_maker = false;
};

}  // namespace te
