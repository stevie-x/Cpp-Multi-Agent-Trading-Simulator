#pragma once
#include <string>
#include <fstream>
#include <sstream>

// MarketTick — one row of OHLCV market data.
// Currently uses close price only. timestamp, open, high, low, volume
// are available for future volatility / ATR calculations.
struct MarketTick {
    long long timestamp;
    double    open;
    double    high;
    double    low;
    double    price;   // close price
    double    volume;
};

// MarketData — streaming CSV iterator.
//
// FIX 19: no longer loads the entire CSV into RAM upfront.
//   Old: std::vector<MarketTick> ticks — all ~100k rows read into memory
//        before tick 1 runs. 6MB ETH file = ~100k rows all in RAM.
//        Couldn't handle live/streaming data at all.
//   New: file stays open, one line parsed per next() call.
//        Memory usage is O(1) regardless of file size.
//        Interview answer to "how would you go live?":
//        replace ifstream with a socket reader — same iterator interface,
//        zero changes to the simulation loop.
//
// The ring_buffer.hpp producer-thread pattern slots in here naturally:
//   Thread 1 reads from socket → pushes MarketTick to RingBuffer
//   Thread 2 (sim loop) calls next() → pops from RingBuffer
// This class is the file-backed version of that same interface.

class MarketData {
public:
    MarketData() = default;
    ~MarketData() { if (file_.is_open()) file_.close(); }

    // loadCSV — opens file and positions at first data row.
    // Returns false if file cannot be opened.
    bool loadCSV(const std::string& filename) {
        file_.open(filename);
        if (!file_.is_open()) return false;
        // Skip header row if present
        // Detect by checking if first field parses as a number
        std::string firstLine;
        if (std::getline(file_, firstLine)) {
            std::stringstream ss(firstLine);
            std::string token;
            std::getline(ss, token, ',');
            try {
                std::stoll(token);
                // It's a number — this is data, not a header.
                // Push back by re-opening and seeking, or store and use next tick.
                pending_     = parseLine(firstLine);
                hasPending_  = true;
            } catch (...) {
                // It's a header row — skip it, read normally from here
                hasPending_ = false;
            }
        }
        return true;
    }

    bool hasNext() {
        if (hasPending_) return true;
        return file_.is_open() && file_.peek() != EOF;
    }

    MarketTick next() {
        if (hasPending_) {
            hasPending_ = false;
            return pending_;
        }
        std::string line;
        std::getline(file_, line);
        return parseLine(line);
    }

private:
    std::ifstream file_;
    bool          hasPending_ = false;
    MarketTick    pending_{};

    static MarketTick parseLine(const std::string& line) {
        std::stringstream ss(line);
        std::string token;
        MarketTick tick{};

        // Column order: timestamp, open, high, low, close, volume
        std::getline(ss, token, ','); tick.timestamp = std::stoll(token);
        std::getline(ss, token, ','); tick.open      = std::stod(token);
        std::getline(ss, token, ','); tick.high      = std::stod(token);
        std::getline(ss, token, ','); tick.low       = std::stod(token);
        std::getline(ss, token, ','); tick.price     = std::stod(token);  // close
        if (std::getline(ss, token, ',')) {
            try { tick.volume = std::stod(token); } catch (...) { tick.volume = 0.0; }
        }
        return tick;
    }
};