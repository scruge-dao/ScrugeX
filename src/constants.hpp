#pragma once
#include <eosiolib/eosio.hpp>
#include <eosiolib/asset.hpp>
#include <string>

using namespace eosio;
using namespace std;

// CONSTANTS

const symbol& EOS_SYMBOL = symbol{"EOS", 4};

const uint64_t SECOND = 1000;
const uint64_t MINUTE = 60 * SECOND;
const uint64_t HOUR = 60 * MINUTE;
const uint64_t DAY = 24 * HOUR;

// threshold of votes for releasing funds
const uint64_t T1 = 66; 

// threshold of votes for extending deadline
const uint64_t T2 = 66; 

// todo this should be an argument in extend action
// and be saved inside a milestone/vote in question
const uint64_t TIMET = 14 * DAY; 
