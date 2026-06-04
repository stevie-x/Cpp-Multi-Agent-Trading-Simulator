CXX      := g++
CXXFLAGS := -std=c++17 -O3 -march=native -funroll-loops \
            -Wall -Wextra -Wno-unused-parameter \
            -Isrc -Iagents \
            -I/opt/homebrew/include \
            -I/opt/homebrew/opt/openssl@3/include

LDFLAGS  := -L/opt/homebrew/lib \
            -L/opt/homebrew/opt/openssl@3/lib \
            -lwebsockets -lssl -lcrypto -lpthread

SRCS := src/main.cpp \
        src/matching_engine.cpp \
        src/market_data.cpp \
        src/order.cpp \
        agents/bot.cpp \
        agents/momentum_bot.cpp

TARGET := trading_sim

.PHONY: all clean run

all: $(TARGET)

$(TARGET): $(SRCS)
	$(CXX) $(CXXFLAGS) -o $@ $^ $(LDFLAGS)
	@echo "Built with O3 + march=native + libwebsockets"

run: all
	./$(TARGET)

clean:
	rm -f $(TARGET)