CXX      = g++
CXXFLAGS = -std=c++17 -O2 -Wall -Wextra -Isrc -Iagents -pthread

# ── Simulator (main.cpp — single-threaded vs multithreaded benchmark) ─────────
SRCS = src/main.cpp \
       src/market_data.cpp \
       src/matching_engine.cpp \
       agents/bot.cpp \
       agents/momentum_bot.cpp

TARGET = trading_sim

all: $(TARGET)

$(TARGET): $(SRCS)
	$(CXX) $(CXXFLAGS) $(SRCS) -o $(TARGET)

run: all
	./$(TARGET)

# ── HTTP server (the actual matchable engine submitted to the hackathon) ───────
# This is the binary you upload — it listens on :8080 and exposes:
#   POST /order         — place limit, market, or cancel orders
#   GET  /orderbook     — best bid, best ask, spread
#   GET  /orderbook/depth — top 5 levels each side
SERVER_SRCS = src/http_server.cpp \
              src/orderbook.cpp \
              src/market_data.cpp \
              agents/bot.cpp \
              agents/momentum_bot.cpp

server: $(SERVER_SRCS)
	$(CXX) $(CXXFLAGS) $(SERVER_SRCS) -o trading-server

run-server: server
	./trading-server

# ── Linux cross-compile (for hackathon submission) ────────────────────────────
# Produces a static Linux x86-64 binary that runs inside the sandbox container.
# Run this on Linux or WSL — not on Mac.
linux: $(SERVER_SRCS)
	$(CXX) $(CXXFLAGS) -static $(SERVER_SRCS) -o trading-server-linux

# ── Profiling build ───────────────────────────────────────────────────────────
profile: $(SRCS)
	$(CXX) $(CXXFLAGS) -pg $(SRCS) -o trading_sim_profile
	./trading_sim_profile
	gprof trading_sim_profile gmon.out > profile.txt
	@echo "Profile written to profile.txt"

# ── Tests ─────────────────────────────────────────────────────────────────────
TEST_SRCS = tests/test_orderbook.cpp \
            src/market_data.cpp \
            src/matching_engine.cpp \
            agents/bot.cpp \
            agents/momentum_bot.cpp

test: $(TEST_SRCS)
	$(CXX) $(CXXFLAGS) $(TEST_SRCS) -o run_tests
	./run_tests

# ── Clean ─────────────────────────────────────────────────────────────────────
clean:
	rm -f $(TARGET) trading-server trading-server-linux trading_sim_profile run_tests gmon.out