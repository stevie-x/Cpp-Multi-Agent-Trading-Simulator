CXX = g++
CXXFLAGS = -std=c++17 -Wall -Isrc -Iagents

SRCS = src/main.cpp src/market_data.cpp src/matching_engine.cpp agents/momentum_bot.cpp
TARGET = trading_sim

all: $(TARGET)

$(TARGET): $(SRCS)
	$(CXX) $(CXXFLAGS) $(SRCS) -o $(TARGET)

run: all
	./$(TARGET)

clean:
	rm -f $(TARGET)