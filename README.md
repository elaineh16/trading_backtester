# Trading Strategy Backtester

> **In progress:** this project is evolving into an event-driven algorithmic trading system with a
> C++ engine, tick-level crypto data, market-making and momentum strategies, and Redis + WebSocket
> streaming. The new engine lives in [`engine/`](engine/README.md); the Python backtester below is
> becoming its research and analysis layer.

A comprehensive Python-based trading strategy backtesting framework for evaluating and optimizing algorithmic trading strategies.

## Features

- **Multiple Strategy Support**: Implement and test various trading strategies (Moving Averages, RSI, MACD, etc.)
- **Historical Data Integration**: Support for CSV files and real-time data via yfinance
- **Performance Analysis**: Comprehensive metrics including Sharpe ratio, drawdown, win rate
- **Visualization**: Interactive charts and performance dashboards
- **Jupyter Integration**: Notebooks for strategy development and analysis
- **Modular Architecture**: Clean, extensible codebase for easy strategy development

## Project Structure

```
trading-backtester/
│── data/                # Store historical data
│── strategies/          # Your trading strategies
│── backtester/          # Core backtesting engine
│── analysis/            # Performance evaluation + visualizations
│── notebooks/           # Jupyter notebooks for exploration
│── main.py              # Entry point
│── requirements.txt     # Dependencies
│── README.md            # Project overview
```

## Installation

1. Clone the repository:
```bash
git clone <repository-url>
cd trading-backtester
```

2. Create a virtual environment:
```bash
python -m venv venv
source venv/bin/activate  # On Windows: venv\Scripts\activate
```

3. Install dependencies:
```bash
pip install -r requirements.txt
```

## Quick Start

1. **List available strategies**:
```bash
python main.py --list-strategies
```

2. **Run a backtest**:
```bash
python main.py --strategy moving_average --data data/AAPL.csv
```

3. **Run with custom parameters**:
```bash
python main.py --strategy rsi --data data/SPY.csv --start-date 2020-01-01 --initial-capital 50000
```

## Usage Examples

### Basic Backtest
```bash
python main.py --strategy moving_average --data data/AAPL.csv --initial-capital 100000
```

### Advanced Backtest with Date Range
```bash
python main.py \
    --strategy rsi \
    --data data/SPY.csv \
    --start-date 2020-01-01 \
    --end-date 2023-12-31 \
    --initial-capital 100000 \
    --commission 0.001 \
    --output results.json
```

### List All Strategies
```bash
python main.py --list-strategies
```

## Strategy Development

### Creating a New Strategy

1. Create a new file in `strategies/` directory
2. Implement the `BaseStrategy` interface
3. Define your entry/exit logic
4. Register the strategy in the strategy loader

Example strategy structure:
```python
from strategies.base_strategy import BaseStrategy

class MyStrategy(BaseStrategy):
    def __init__(self, parameters):
        super().__init__(parameters)
    
    def generate_signals(self, data):
        # Implement your signal generation logic
        pass
    
    def calculate_position_size(self, signal, portfolio):
        # Implement position sizing logic
        pass
```

## Data Format

The backtester expects CSV files with the following columns:
- `Date`: Date in YYYY-MM-DD format
- `Open`: Opening price
- `High`: Highest price of the day
- `Low`: Lowest price of the day
- `Close`: Closing price
- `Volume`: Trading volume

## Performance Metrics

The backtester calculates the following metrics:
- **Total Return**: Overall percentage return
- **Annualized Return**: Yearly return rate
- **Sharpe Ratio**: Risk-adjusted return measure
- **Maximum Drawdown**: Largest peak-to-trough decline
- **Win Rate**: Percentage of profitable trades
- **Total Trades**: Number of executed trades

## Jupyter Notebooks

Explore the `notebooks/` directory for:
- Strategy development examples
- Data analysis workflows
- Performance visualization
- Risk analysis

## Contributing

1. Fork the repository
2. Create a feature branch
3. Implement your changes
4. Add tests
5. Submit a pull request

## Testing

Run the test suite:
```bash
pytest tests/
```

Run with coverage:
```bash
pytest --cov=backtester --cov=strategies --cov=analysis tests/
```

## License

This project is licensed under the MIT License - see the LICENSE file for details.

## Disclaimer

This software is for educational and research purposes only. Past performance does not guarantee future results. Always do your own research and consider consulting with a financial advisor before making investment decisions.

## Support

For questions and support:
- Create an issue on GitHub
- Check the documentation in the `notebooks/` directory
- Review example strategies in the `strategies/` directory
