"""Research layer on top of the C++ engine: experiments, sweeps and plots."""

from .experiments import (
    SplitResult,
    compare,
    config_sweep,
    param_grid,
    run_grid,
    select_and_validate,
    time_split,
)

__all__ = [
    "SplitResult",
    "compare",
    "config_sweep",
    "param_grid",
    "run_grid",
    "select_and_validate",
    "time_split",
]
