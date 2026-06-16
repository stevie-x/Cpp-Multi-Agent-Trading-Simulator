// market_data.cpp
//
// FIX 19: MarketData is now fully header-only (market_data.hpp).
// This file is intentionally empty — kept so the Makefile doesn't need
// to be changed and existing build scripts continue to work.
//
// All implementation moved to market_data.hpp as inline methods.
// This is valid C++ — small classes with file I/O are commonly header-only.