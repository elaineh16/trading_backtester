import sys
from pathlib import Path

import pandas as pd
import pytest

ROOT = Path(__file__).resolve().parents[1]
for p in (ROOT, ROOT / "engine" / "python", ROOT / "tools"):
    if str(p) not in sys.path:
        sys.path.insert(0, str(p))

from generate_synthetic_trades import generate, write_csv  # noqa: E402


@pytest.fixture(scope="session")
def tape_path(tmp_path_factory) -> Path:
    path = tmp_path_factory.mktemp("data") / "SYNTH-test.csv"
    write_csv(generate(150_000, seed=7), path)
    return path


@pytest.fixture(scope="session")
def trades(tape_path) -> pd.DataFrame:
    import trading_engine as te

    return te.load_trades(tape_path)
