#pragma once
#include "flat_orderbook.hpp"
#include "../agents/bot.hpp"
#include <string>
#include <vector>
#include <memory>
#include <unordered_map>

// matchOrdersFlat — same interface as matchOrders but operates on FlatOrderBook.
//
// botIndex: O(1) lookup map built once by the caller and passed in.
// Same fix as matching_engine.hpp — no O(N) findBot() linear scan.

void matchOrdersFlat(FlatOrderBook& lob,
                     std::vector<std::unique_ptr<Bot>>& bots,
                     std::unordered_map<std::string, Bot*>& botIndex,
                     std::string& tradeBuffer,
                     int timestep);