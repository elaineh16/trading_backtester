#pragma once

#include <string>

#include "engine/portfolio.hpp"
#include "engine/types.hpp"

namespace te {

struct SignalUpdate {
    Timestamp ts;
    std::string strategy;
    std::string name;
    double value;
};

// Observer for everything the engine does. Backtests usually run with no
// sinks; live mode attaches a Redis publisher so the WebSocket gateway can
// stream signals and portfolio updates to browsers.
class EventSink {
public:
    virtual ~EventSink() = default;
    virtual void on_trade(const MarketTrade&) {}
    virtual void on_order(const Order&) {}
    virtual void on_fill(const Fill&, const Portfolio&) {}
    virtual void on_signal(const SignalUpdate&) {}
    virtual void on_portfolio(Timestamp, const Portfolio&) {}
};

}  // namespace te
