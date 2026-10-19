#include "engine/sim_exchange.hpp"

#include <algorithm>
#include <cmath>

namespace te {

namespace {
constexpr double kEps = 1e-12;
}

Timestamp SimExchange::submit(Order order, Timestamp now) {
    order.submit_ts = now;
    order.active_ts = now + cfg_.order_latency_us;
    order.status = OrderStatus::PendingNew;
    order.filled = 0.0;
    const Timestamp active = order.active_ts;
    pending_.push_back(order.id);
    orders_[order.id] = order;
    return active;
}

void SimExchange::activate(OrderId id) {
    auto it = orders_.find(id);
    if (it == orders_.end() || it->second.status != OrderStatus::PendingNew) return;
    it->second.status = OrderStatus::Live;
    remove_id(pending_, id);
    live_.push_back(id);
}

Timestamp SimExchange::request_cancel(OrderId id, Timestamp now) {
    auto it = orders_.find(id);
    if (it == orders_.end() || !it->second.is_open()) return -1;
    return now + cfg_.cancel_latency_us;
}

bool SimExchange::apply_cancel(OrderId id) {
    auto it = orders_.find(id);
    if (it == orders_.end() || !it->second.is_open()) return false;
    it->second.status = OrderStatus::Cancelled;
    remove_id(pending_, id);
    remove_id(live_, id);
    return true;
}

double SimExchange::fee_for(double notional, bool maker) const {
    return notional * (maker ? cfg_.maker_fee_bps : cfg_.taker_fee_bps) * 1e-4;
}

void SimExchange::remove_id(std::vector<OrderId>& v, OrderId id) {
    auto it = std::find(v.begin(), v.end(), id);
    if (it != v.end()) {
        *it = v.back();
        v.pop_back();
    }
}

void SimExchange::on_trade(const MarketTrade& trade, std::vector<Fill>& out) {
    // Iterate over a snapshot: filled orders are removed from live_ as we go.
    std::size_t i = 0;
    double tape_qty_left = trade.qty;  // a single print can't fill more than its size
    while (i < live_.size()) {
        Order& o = orders_[live_[i]];
        double fill_qty = 0.0;
        double fill_px = 0.0;
        bool maker = true;

        if (o.type == OrderType::Market) {
            // Taker: crosses at the next print, pays slippage.
            const double slip = cfg_.market_slippage_bps * 1e-4;
            fill_px = trade.price * (o.side == Side::Buy ? 1.0 + slip : 1.0 - slip);
            fill_qty = o.remaining();  // assume enough liquidity behind the print
            maker = false;
        } else if (tape_qty_left > kEps) {
            const bool buy = o.side == Side::Buy;
            const bool through = buy ? trade.price < o.price - kEps : trade.price > o.price + kEps;
            const bool at_touch = std::fabs(trade.price - o.price) <= kEps;
            const bool hits_us = buy ? trade.aggressor() == Side::Sell : trade.aggressor() == Side::Buy;
            double available = 0.0;
            if (through) {
                available = tape_qty_left;
            } else if (at_touch && hits_us) {
                available = tape_qty_left * cfg_.touch_fill_ratio;
            }
            fill_qty = std::min(o.remaining(), available);
            fill_px = o.price;
            tape_qty_left -= fill_qty;
        }

        if (fill_qty > kEps) {
            o.filled += fill_qty;
            Fill f;
            f.order_id = o.id;
            f.ts = trade.ts;
            f.side = o.side;
            f.price = fill_px;
            f.qty = fill_qty;
            f.is_maker = maker;
            f.fee = fee_for(fill_px * fill_qty, maker);
            out.push_back(f);

            if (o.remaining() <= kEps) {
                o.status = OrderStatus::Filled;
                live_[i] = live_.back();
                live_.pop_back();
                continue;  // re-examine the order swapped into slot i
            }
            o.status = OrderStatus::PartiallyFilled;
        }
        ++i;
    }
}

const Order* SimExchange::find(OrderId id) const {
    auto it = orders_.find(id);
    return it == orders_.end() ? nullptr : &it->second;
}

std::vector<Order> SimExchange::open_orders() const {
    std::vector<Order> out;
    out.reserve(pending_.size() + live_.size());
    for (OrderId id : pending_) out.push_back(orders_.at(id));
    for (OrderId id : live_) out.push_back(orders_.at(id));
    return out;
}

}  // namespace te
