#pragma once

#include <string>

#include "engine/engine.hpp"

namespace te {

// Writes summary.json, fills.csv and equity.csv into `dir` (created if missing).
void write_results(const BacktestResult& r, const std::string& dir);

std::string format_summary(const BacktestResult& r);

}  // namespace te
