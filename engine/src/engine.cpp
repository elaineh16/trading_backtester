#include "engine/engine.hpp"

#include <chrono>
#include <cmath>
#include <type_traits>

namespace te {

Engine::Engine(EngineConfig cfg) : cfg_(cfg), exchange_(cfg.exchange), portfolio_(cfg.initial_cash) {}

// ---------------------------------------------------------------- Context API

bool Engine::passes_risk(Side side, double qty) const {
    if (qty <= 0.0 || qty > cfg_.risk.max_order_qty + 1e-12) return false;

    const auto open = exchange_.open_orders();
    if (open.size() >= cfg_.risk.max_open_orders) return false;

    // Worst case: every open order on this side fills, plus this one.
    double same_side = 0.0;
    for (const auto& o : open)
        if (o.side == side) same_side += o.remaining();
    const double worst = portfolio_.position() + sign(side) * (same_side + qty);
    return std::fabs(worst) <= cfg_.risk.max_position + 1e-12;
}

OrderId Engine::submit(Side side, OrderType type, double price, double qty) {
    if (!passes_risk(side, qty)) {
        ++rejected_;
        return 0;
    }
    Order o;
    o.id = next_order_id_++;
    o.side = side;
    o.type = type;
    o.price = price;
    o.qty = qty;
    const Timestamp active_at = exchange_.submit(o, now_);
    queue_.push(active_at, OrderActivateEvent{o.id});
    ++orders_sent_;
    if (!sinks_.empty()) {
        const Order* stored = exchange_.find(o.id);
        for (auto* s : sinks_) s->on_order(*stored);
    }
    return o.id;
}

OrderId Engine::submit_limit(Side side, double price, double qty) {
    return submit(side, OrderType::Limit, price, qty);
}

OrderId Engine::submit_market(Side side, double qty) { return submit(side, OrderType::Market, 0.0, qty); }

void Engine::cancel(OrderId id) {
    const Timestamp at = exchange_.request_cancel(id, now_);
    if (at >= 0) queue_.push(at, CancelActivateEvent{id});
}

void Engine::cancel_all() {
    for (const auto& o : exchange_.open_orders()) cancel(o.id);
}

void Engine::schedule_timer(Timestamp at, std::uint64_t timer_id) { queue_.push(at, TimerEvent{timer_id}); }

void Engine::emit_signal(const std::string& name, double value) {
    if (sinks_.empty()) return;
    SignalUpdate u{now_, strategy_ ? strategy_->name() : std::string{}, name, value};
    for (auto* s : sinks_) s->on_signal(u);
}

// ---------------------------------------------------------------- event loop

void Engine::handle_event(const Event& ev) {
    ++events_processed_;
    std::visit(
        [&](const auto& e) {
            using T = std::decay_t<decltype(e)>;
            if constexpr (std::is_same_v<T, OrderActivateEvent>) {
                exchange_.activate(e.order_id);
            } else if constexpr (std::is_same_v<T, CancelActivateEvent>) {
                exchange_.apply_cancel(e.order_id);
            } else if constexpr (std::is_same_v<T, TimerEvent>) {
                strategy_->on_timer(e.timer_id, *this);
            }
        },
        ev.payload);
}

void Engine::handle_trade(const MarketTrade& trade) {
    ++trades_processed_;
    ++events_processed_;

    // Sample equity at interval boundaries using the state *before* this print.
    while (trade.ts >= next_equity_sample_) {
        portfolio_.record_equity(next_equity_sample_);
        next_equity_sample_ += cfg_.equity_sample_us;
    }

    portfolio_.mark(trade.ts, trade.price);
    for (auto* s : sinks_) s->on_trade(trade);

    // 1) The exchange matches orders that were already live before this print.
    scratch_fills_.clear();
    exchange_.on_trade(trade, scratch_fills_);
    for (const Fill& f : scratch_fills_) {
        portfolio_.apply_fill(f);
        if (cfg_.record_fills) fills_.push_back(f);
        for (auto* s : sinks_) s->on_fill(f, portfolio_);
        strategy_->on_fill(f, *this);
    }

    // 2) Only then does the strategy see the print. Anything it sends now
    //    becomes live after latency, so it can only fill on *later* prints.
    strategy_->on_trade(trade, *this);

    if (!sinks_.empty() && trade.ts >= next_publish_) {
        for (auto* s : sinks_) s->on_portfolio(trade.ts, portfolio_);
        next_publish_ = trade.ts + cfg_.portfolio_publish_us;
    }
}

BacktestResult Engine::run(DataSource& source, Strategy& strategy) {
    strategy_ = &strategy;
    const auto wall_start = std::chrono::steady_clock::now();

    MarketTrade trade;
    bool has_trade = source.next(trade);
    double first_price = 0.0;
    double last_price = 0.0;
    Timestamp first_ts = 0;

    if (has_trade) {
        now_ = trade.ts;
        first_ts = trade.ts;
        first_price = trade.price;
        portfolio_.mark(trade.ts, trade.price);  // so the first equity sample has a valid mark
        next_equity_sample_ = trade.ts;
        next_publish_ = trade.ts;
    }
    strategy.on_start(*this);

    while (has_trade || !queue_.empty()) {
        if (!queue_.empty() && (!has_trade || queue_.top().ts <= trade.ts)) {
            Event ev = queue_.pop();
            now_ = std::max(now_, ev.ts);
            handle_event(ev);
            continue;
        }
        if (trade.ts < now_) {
            ++out_of_order_;  // tape went backwards; clamp rather than rewind the clock
            trade.ts = now_;
        }
        now_ = trade.ts;
        last_price = trade.price;
        handle_trade(trade);
        has_trade = source.next(trade);
        // Once the tape ends, stop: pending orders/timers can't be evaluated without data.
        if (!has_trade) break;
    }

    strategy.on_end(*this);
    portfolio_.record_equity(now_);

    const double wall =
        std::chrono::duration<double>(std::chrono::steady_clock::now() - wall_start).count();

    BacktestResult r;
    r.strategy = strategy.name();
    Metrics& m = r.metrics;
    compute_portfolio_metrics(portfolio_, fills_, cfg_.equity_sample_us, first_price, last_price, m);
    m.orders_sent = orders_sent_;
    m.orders_rejected = rejected_;
    m.market_trades = trades_processed_;
    m.events = events_processed_;
    m.duration_days = static_cast<double>(now_ - first_ts) / (86400.0 * kMicrosPerSecond);
    m.wall_seconds = wall;
    m.events_per_second = wall > 0 ? static_cast<double>(events_processed_) / wall : 0.0;
    r.fills = fills_;
    r.equity_curve = portfolio_.equity_curve();
    return r;
}

}  // namespace te
