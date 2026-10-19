#include "engine/strategies/registry.hpp"

#include <stdexcept>

#include "engine/strategies/market_maker.hpp"
#include "engine/strategies/momentum.hpp"

namespace te {

std::unique_ptr<Strategy> make_strategy(const std::string& name, const Params& params) {
    if (name == "market_maker" || name == "mm") return std::make_unique<MarketMaker>(params);
    if (name == "momentum" || name == "mom") return std::make_unique<Momentum>(params);
    throw std::invalid_argument("unknown strategy: " + name);
}

std::vector<std::string> strategy_names() { return {"market_maker", "momentum"}; }

}  // namespace te
