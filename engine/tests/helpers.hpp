#pragma once

#include <cmath>
#include <random>
#include <vector>

#include "engine/types.hpp"

namespace te::test {

// buyer_is_maker = true  -> seller-initiated print (hits bids)
inline MarketTrade trade(Timestamp ts, double price, double qty = 1.0, bool buyer_is_maker = false) {
    MarketTrade t;
    t.ts = ts;
    t.price = price;
    t.qty = qty;
    t.buyer_is_maker = buyer_is_maker;
    return t;
}

constexpr Timestamp ms(double v) { return static_cast<Timestamp>(v * 1000); }
constexpr Timestamp sec(double v) { return static_cast<Timestamp>(v * 1'000'000); }

// Random-walk tape with bid/ask bounce, optional drift (bps per trade).
inline std::vector<MarketTrade> random_tape(std::size_t n, double start = 50'000.0, double drift_bps = 0.0,
                                            unsigned seed = 42, Timestamp dt_us = 100'000) {
    std::mt19937_64 rng(seed);
    std::normal_distribution<double> step(0.0, 1.0);
    std::bernoulli_distribution coin(0.5);
    std::exponential_distribution<double> size(50.0);
    std::vector<MarketTrade> out;
    out.reserve(n);
    double mid = start;
    for (std::size_t i = 0; i < n; ++i) {
        mid *= 1.0 + (drift_bps + step(rng)) * 1e-4;
        const bool sell = coin(rng);
        const double px = std::round(mid * (sell ? 1.0 - 0.5e-4 : 1.0 + 0.5e-4) * 100.0) / 100.0;
        out.push_back(trade(static_cast<Timestamp>(i) * dt_us + 1'700'000'000'000'000LL, px, 0.001 + size(rng), sell));
    }
    return out;
}

}  // namespace te::test
