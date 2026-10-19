#pragma once

#include <cstdint>
#include <queue>
#include <variant>
#include <vector>

#include "engine/types.hpp"

namespace te {

// Internal (non-market-data) events. Market trades are streamed from the data
// source and merged with these by timestamp inside the engine loop.
struct OrderActivateEvent {
    OrderId order_id;
};
struct CancelActivateEvent {
    OrderId order_id;
};
struct TimerEvent {
    std::uint64_t timer_id;
};

using EventPayload = std::variant<OrderActivateEvent, CancelActivateEvent, TimerEvent>;

struct Event {
    Timestamp ts;
    std::uint64_t seq;  // insertion order; breaks timestamp ties FIFO
    EventPayload payload;
};

// Min-heap of events ordered by (timestamp, insertion sequence).
// Deterministic: two events at the same timestamp are processed in the order
// they were scheduled, so backtests are exactly reproducible.
class EventQueue {
public:
    void push(Timestamp ts, EventPayload payload) { heap_.push(Event{ts, next_seq_++, std::move(payload)}); }

    bool empty() const { return heap_.empty(); }
    std::size_t size() const { return heap_.size(); }
    const Event& top() const { return heap_.top(); }

    Event pop() {
        Event e = heap_.top();
        heap_.pop();
        return e;
    }

private:
    struct Later {
        bool operator()(const Event& a, const Event& b) const {
            if (a.ts != b.ts) return a.ts > b.ts;
            return a.seq > b.seq;
        }
    };
    std::priority_queue<Event, std::vector<Event>, Later> heap_;
    std::uint64_t next_seq_ = 0;
};

}  // namespace te
