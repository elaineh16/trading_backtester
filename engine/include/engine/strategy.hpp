#pragma once

#include <cstdint>
#include <map>
#include <string>
#include <vector>

#include "engine/portfolio.hpp"
#include "engine/types.hpp"

namespace te {

// What a strategy is allowed to see and do. Implemented by the Engine.
// Strategies never touch the exchange or the clock directly, so the same
// strategy code runs unchanged in backtest and live mode.
class Context {
public:
    virtual ~Context() = default;

    virtual Timestamp now() const = 0;

    // Returns the new order id, or 0 if the order was rejected by risk limits.
    virtual OrderId submit_limit(Side side, double price, double qty) = 0;
    virtual OrderId submit_market(Side side, double qty) = 0;
    virtual void cancel(OrderId id) = 0;
    virtual void cancel_all() = 0;

    virtual double position() const = 0;
    virtual const Portfolio& portfolio() const = 0;
    virtual std::vector<Order> open_orders() const = 0;

    virtual void schedule_timer(Timestamp at, std::uint64_t timer_id) = 0;

    // Publish a named strategy signal (fair value, momentum score, ...).
    // Routed to event sinks, e.g. Redis -> WebSocket clients.
    virtual void emit_signal(const std::string& name, double value) = 0;
};

using Params = std::map<std::string, double>;

class Strategy {
public:
    virtual ~Strategy() = default;
    virtual std::string name() const = 0;

    virtual void on_start(Context&) {}
    virtual void on_trade(const MarketTrade& trade, Context& ctx) = 0;
    virtual void on_fill(const Fill&, Context&) {}
    virtual void on_timer(std::uint64_t /*timer_id*/, Context&) {}
    virtual void on_end(Context&) {}

protected:
    static double param(const Params& p, const std::string& key, double fallback) {
        auto it = p.find(key);
        return it == p.end() ? fallback : it->second;
    }
};

}  // namespace te
