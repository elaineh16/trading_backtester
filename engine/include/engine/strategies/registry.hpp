#pragma once

#include <memory>
#include <string>
#include <vector>

#include "engine/strategy.hpp"

namespace te {

// Creates a strategy by name ("market_maker" / "mm", "momentum" / "mom").
// Throws std::invalid_argument for unknown names.
std::unique_ptr<Strategy> make_strategy(const std::string& name, const Params& params = {});
std::vector<std::string> strategy_names();

}  // namespace te
