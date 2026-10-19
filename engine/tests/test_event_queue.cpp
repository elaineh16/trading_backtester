#include <gtest/gtest.h>

#include "engine/event_queue.hpp"

using namespace te;

TEST(EventQueue, PopsInTimestampOrder) {
    EventQueue q;
    q.push(300, TimerEvent{3});
    q.push(100, TimerEvent{1});
    q.push(200, TimerEvent{2});
    EXPECT_EQ(std::get<TimerEvent>(q.pop().payload).timer_id, 1u);
    EXPECT_EQ(std::get<TimerEvent>(q.pop().payload).timer_id, 2u);
    EXPECT_EQ(std::get<TimerEvent>(q.pop().payload).timer_id, 3u);
    EXPECT_TRUE(q.empty());
}

TEST(EventQueue, TiesAreFifoForDeterminism) {
    EventQueue q;
    for (std::uint64_t i = 0; i < 100; ++i) q.push(500, TimerEvent{i});
    for (std::uint64_t i = 0; i < 100; ++i) EXPECT_EQ(std::get<TimerEvent>(q.pop().payload).timer_id, i);
}
