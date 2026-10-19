#pragma once

#include <unordered_map>
#include <vector>

#include "engine/types.hpp"

namespace te {

struct ExchangeConfig {
    Timestamp order_latency_us = 5'000;   // strategy -> exchange, one way
    Timestamp cancel_latency_us = 5'000;  // an order can still fill while its cancel is in flight
    double maker_fee_bps = 1.0;           // negative = rebate
    double taker_fee_bps = 4.0;
    double market_slippage_bps = 1.0;     // extra cost on top of the next trade price for market orders
    // Queue-position model: when a public trade prints exactly at our resting
    // price, we only get this fraction of it (we are not first in the queue).
    // Trades strictly through our price fill us fully.
    double touch_fill_ratio = 0.25;
};

// Simulated exchange that matches the strategy's orders against the public
// trade tape. It never sees the future: orders only become eligible to fill
// once their latency has elapsed, and they can only fill against trades that
// print after that.
//
// Fill rules for a resting BUY limit at price P (mirror for sells):
//   * trade price < P                       -> fill (market traded through us)
//   * trade price == P and seller-initiated -> partial fill (touch_fill_ratio)
//   * otherwise                             -> no fill
// Market orders fill at the next trade price plus slippage, as taker.
class SimExchange {
public:
    explicit SimExchange(ExchangeConfig cfg = {}) : cfg_(cfg) {}

    const ExchangeConfig& config() const { return cfg_; }

    // Registers an order sent at `now`. Returns the time it becomes live.
    Timestamp submit(Order order, Timestamp now);
    void activate(OrderId id);

    // Returns the time the cancel takes effect, or -1 if the order is unknown/closed.
    Timestamp request_cancel(OrderId id, Timestamp now);
    // Returns true if the order was still open and is now cancelled.
    bool apply_cancel(OrderId id);

    // Matches all live orders against one public trade. Appends fills to `out`.
    void on_trade(const MarketTrade& trade, std::vector<Fill>& out);

    const Order* find(OrderId id) const;
    std::vector<Order> open_orders() const;
    std::size_t live_count() const { return live_.size(); }

private:
    double fee_for(double notional, bool maker) const;
    static void remove_id(std::vector<OrderId>& v, OrderId id);

    ExchangeConfig cfg_;
    std::unordered_map<OrderId, Order> orders_;
    std::vector<OrderId> pending_;  // sent, latency not yet elapsed
    std::vector<OrderId> live_;     // resting / working at the exchange
};

}  // namespace te
