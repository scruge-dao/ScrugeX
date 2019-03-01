#pragma once
#include <eosiolib/eosio.hpp>
#include <eosiolib/asset.hpp>
#include <string>

using namespace eosio;
using namespace std;

// METHODS

bool is_number(const string& s) {
	return !s.empty() && s.find_first_not_of("0123456789") == string::npos;
}

uint64_t time_ms() {
	return current_time() / 1000;
}

uint64_t get_percent(uint64_t value, uint64_t percent) { 
	return value * percent / 100;
}

asset get_percent(asset quantity, uint64_t percent) {
	auto amount = get_percent(quantity.amount, percent);
	return asset(amount, quantity.symbol);
}
