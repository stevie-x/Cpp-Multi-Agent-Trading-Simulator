#pragma once
#include "orderbook.hpp"
#include <string>

void matchOrders(LimitOrderBook &lob, std::string &tradeBuffer, int timestep);