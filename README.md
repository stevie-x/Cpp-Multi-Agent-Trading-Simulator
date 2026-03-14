#  C++ Multi-Agent Trading Simulator

A high-performance, event-driven trading simulator written in C++ that models multiple autonomous trading agents competing in a real-time limit order book using historical cryptocurrency market data.

---

##  Overview

This project simulates a realistic financial exchange where multiple bots place buy/sell orders based on different strategies. Orders are matched through a **Limit Order Book (LOB)** engine using real **ETH/USDT** 1-minute candlestick data from Binance.

---

##  Features

- ⚙️ **Limit Order Book** — price-time priority matching engine
- 🤖 **Multi-Agent System** — multiple bots trading simultaneously
- 📊 **Real Market Data** — plugs into Binance historical CSV data
- 🎲 **Random Bot** — places randomized buy/sell orders around market price
- 📉 **Momentum Bot** — trend-following strategy using a rolling price window
- 🔁 **Event-driven loop** — tick-by-tick simulation over historical data

---

##  Project Structure

```
crypto-trading-simulator/
├── src/
│   ├── main.cpp              # Entry point, simulation loop
│   ├── order.hpp             # Order struct definition
│   ├── orderbook.hpp         # Limit Order Book class
│   ├── matching_engine.hpp   # Matching engine interface
│   ├── matching_engine.cpp   # Order matching logic
│   ├── market_data.hpp       # Market data loader interface
│   └── market_data.cpp       # CSV parser for tick data
├── agents/
│   ├── bot.hpp               # Abstract base Bot class
│   ├── random_bot.hpp        # Random trading agent
│   ├── momentum_bot.hpp      # Momentum-based trading agent
│   └── momentum_bot.cpp      # Momentum bot implementation
├── data/
│   └── eth_1m.csv            # Historical ETH/USDT 1m candle data
├── Makefile
└── README.md
```

---

##  Getting Started

### Prerequisites

- `g++` with C++17 support
- `make`

**Mac:**
```bash
xcode-select --install
```

**Linux/Ubuntu:**
```bash
sudo apt install g++ make
```

**Windows:** Install [MSYS2](https://www.msys2.org/) and run:
```bash
pacman -S mingw-w64-x86_64-gcc make
```

---

### Build & Run

```bash
# Clone the repo
git clone https://github.com/traveller03310/C-Multi-Agent-Trading-Simulator.git
cd C-Multi-Agent-Trading-Simulator

# Build and run
make run
```

### Other commands

```bash
make        # Build only
make clean  # Remove compiled binary
```

---

##  Getting Market Data

This simulator uses Binance 1-minute OHLCV candlestick data.

1. Go to [https://data.binance.vision](https://data.binance.vision)
2. Navigate to `data → spot → monthly → klines → ETHUSDT → 1m`
3. Download any `.zip` file
4. Unzip and rename the CSV to `eth_1m.csv`
5. Place it in the `data/` folder

---

##  Sample Output

```
=== Timestep 1 ===
Price: 2447.83

=== Timestep 2 ===
Price: 2447.65

=== Timestep 3 ===
Price: 2456.96
Trade executed: 1 ETH at 2451.96 between BotA and BotB
```

---

##  Trading Agents

### RandomBot
Places randomized limit orders slightly above or below the current market price. Simulates noise traders in the market.

### MomentumBot
Tracks a rolling window of recent prices. Buys when price is trending up, sells when trending down. Simulates trend-following strategies.

---

##  Roadmap

- [ ] PnL tracking per agent
- [ ] More strategy bots (Mean Reversion, VWAP)
- [ ] Trade history logging to CSV
- [ ] Performance benchmarking across agents
- [ ] Visualization of order book depth

---

##  Built With

- **C++17**
- **STL** — `std::map`, `std::queue` for order book
- **Binance Public Data API** for historical market data

---

##  License

This project is open source and available under the [MIT License](LICENSE).

---

##  Acknowledgements

- [Binance Public Data](https://data.binance.vision) for free historical market data
- Inspired by real-world limit order book implementations used in HFT systems
