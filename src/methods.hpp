#pragma once
#include <eosiolib/eosio.hpp>
#include <eosiolib/asset.hpp>

using namespace eosio;
using namespace std;

#define PRINT(x, y) eosio::print(x); eosio::print(": "); eosio::print(y); eosio::print("\n");
#define PRINT_(x) eosio::print(x); eosio::print("\n");

// METHODS

bool is_number(const string& s) {
	return !s.empty() && s.find_first_not_of("0123456789") == string::npos;
}

uint64_t time_ms() {
	return current_time() / 1'000;
}

uint64_t get_percent(uint64_t value, uint64_t percent) { 
	return value * percent / 100;
}

// to-do think about overflow behaviour
asset get_percent(asset quantity, uint64_t percent) {
	auto amount = get_percent(quantity.amount, percent);
	return asset(amount, quantity.symbol);
}
