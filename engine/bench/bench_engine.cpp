// Engine throughput benchmark on a synthetic tape (no disk I/O in the timed section).
//
//   te_bench [num_trades=5000000]

#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <random>
#include <vector>

#include "engine/engine.hpp"
#include "engine/strategies/registry.hpp"

using namespace te;

static std::vector<MarketTrade> make_tape(std::size_t n) {
    std::mt19937_64 rng(7);
    std::normal_distribution<double> step(0.0, 1.0);
    std::bernoulli_distribution coin(0.5);
    std::exponential_distribution<double> gap(1.0 / 20'000.0);  // ~20ms between prints
    std::vector<MarketTrade> v;
    v.reserve(n);
    double mid = 65'000.0;
    Timestamp ts = 1'760'000'000'000'000LL;
    for (std::size_t i = 0; i < n; ++i) {
        mid *= 1.0 + step(rng) * 0.5e-4;
        ts += static_cast<Timestamp>(gap(rng)) + 1;
        const bool sell = coin(rng);
        MarketTrade t;
        t.ts = ts;
        t.price = std::round(mid * (sell ? 0.99995 : 1.00005) * 100.0) / 100.0;
        t.qty = 0.001 + std::fmod(static_cast<double>(i) * 0.0137, 0.05);
        t.buyer_is_maker = sell;
        t.trade_id = i;
        v.push_back(t);
    }
    return v;
}

int main(int argc, char** argv) {
    const std::size_t n = argc > 1 ? std::strtoull(argv[1], nullptr, 10) : 5'000'000;
    std::printf("Generating %zu synthetic trades...\n", n);
    const auto tape = make_tape(n);

    for (const char* name : {"market_maker", "momentum"}) {
        double best = 1e18;
        Metrics m;
        for (int rep = 0; rep < 3; ++rep) {
            auto strat = make_strategy(name);
            VectorSource src(tape);
            EngineConfig cfg;
            cfg.record_fills = false;
            Engine engine(cfg);
            const auto t0 = std::chrono::steady_clock::now();
            auto r = engine.run(src, *strat);
            const double s = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
            if (s < best) {
                best = s;
                m = r.metrics;
            }
        }
        std::printf("%-13s %8.3f s  %6.2fM events/s  %6.2fM trades/s  ns/trade %.0f  fills %zu  orders %zu\n", name,
                    best, m.events / best / 1e6, n / best / 1e6, best * 1e9 / n, m.fills, m.orders_sent);
    }
    return 0;
}
