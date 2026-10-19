#pragma once

#include <memory>
#include <string>
#include <vector>

#include "engine/data_source.hpp"
#include "engine/event_queue.hpp"
#include "engine/event_sink.hpp"
#include "engine/metrics.hpp"
#include "engine/portfolio.hpp"
#include "engine/sim_exchange.hpp"
#include "engine/strategy.hpp"

namespace te {

struct RiskLimits {
    double max_position = 1.0;      // |position| incl. all open orders on that side, base units
    double max_order_qty = 1.0;     // per order
    std::size_t max_open_orders = 20;
};

struct EngineConfig {
    double initial_cash = 100'000.0;
    ExchangeConfig exchange;
    RiskLimits risk;
    Timestamp equity_sample_us = 60 * kMicrosPerSecond;    // equity-curve resolution for metrics
    Timestamp portfolio_publish_us = 1 * kMicrosPerSecond; // how often sinks get portfolio updates
    bool record_fills = true;
};

struct BacktestResult {
    std::string strategy;
    Metrics metrics;
    std::vector<Fill> fills;
    std::vector<EquityPoint> equity_curve;
};

// Event-driven engine. A single loop merges the market-data stream with
// internally scheduled events (order activations after latency, cancels,
// timers) in strict timestamp order, so nothing a strategy does can be
// affected by data it could not yet have seen.
class Engine final : public Context {
public:
    explicit Engine(EngineConfig cfg = {});

    void add_sink(EventSink* sink) { sinks_.push_back(sink); }

    BacktestResult run(DataSource& source, Strategy& strategy);

    // Context
    Timestamp now() const override { return now_; }
    OrderId submit_limit(Side side, double price, double qty) override;
    OrderId submit_market(Side side, double qty) override;
    void cancel(OrderId id) override;
    void cancel_all() override;
    double position() const override { return portfolio_.position(); }
    const Portfolio& portfolio() const override { return portfolio_; }
    std::vector<Order> open_orders() const override { return exchange_.open_orders(); }
    void schedule_timer(Timestamp at, std::uint64_t timer_id) override;
    void emit_signal(const std::string& name, double value) override;

    std::size_t rejected_orders() const { return rejected_; }

private:
    OrderId submit(Side side, OrderType type, double price, double qty);
    bool passes_risk(Side side, double qty) const;
    void handle_trade(const MarketTrade& trade);
    void handle_event(const Event& ev);

    EngineConfig cfg_;
    SimExchange exchange_;
    Portfolio portfolio_;
    EventQueue queue_;
    std::vector<EventSink*> sinks_;
    Strategy* strategy_ = nullptr;

    Timestamp now_ = 0;
    OrderId next_order_id_ = 1;
    std::size_t rejected_ = 0;
    std::size_t orders_sent_ = 0;
    std::size_t trades_processed_ = 0;
    std::size_t events_processed_ = 0;
    std::size_t out_of_order_ = 0;
    Timestamp next_equity_sample_ = 0;
    Timestamp next_publish_ = 0;
    std::vector<Fill> fills_;
    std::vector<Fill> scratch_fills_;
};

}  // namespace te
