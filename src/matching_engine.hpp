#pragma once
#include "orderbook.hpp"
#include "../agents/bot.hpp"
#include <string>
#include <vector>
#include <memory>
#include <unordered_map>

// matchOrders — runs one full matching pass on the LimitOrderBook.
//
// botIndex: O(1) lookup map built once by the caller (main.cpp) and passed
// in here. Avoids the O(N) linear scan through the bots vector on every fill.
// Key = bot name string, Value = raw non-owning Bot pointer.

void matchOrders(LimitOrderBook& lob,
                 std::vector<std::unique_ptr<Bot>>& bots,
                 std::unordered_map<std::string, Bot*>& botIndex,
                 std::string& tradeBuffer,
                 int timestep);