#pragma once
#include <eosiolib/eosio.hpp>
#include <eosiolib/asset.hpp>

// to-do remove all debug values

const eosio::symbol& EOS_SYMBOL = eosio::symbol{"EOS", 4};

const uint64_t SECOND = 1000;
const uint64_t MINUTE = 60 * SECOND;
const uint64_t HOUR = 60 * MINUTE;
const uint64_t DAY = 24 * HOUR;

// threshold of votes for releasing funds
const uint64_t MILESTONE_VOTING_THRESHOLD = 50; 

// threshold of votes for extending deadline
const uint64_t EXTEND_VOTING_THRESHOLD = 50; 

// duration to wait for founder input after failed milestone voting
const uint64_t WAITING_TIME = 3 * DAY;

// max duration of voting
const uint64_t MIN_VOTING_DURATION = 3 * DAY;

// duration of voting
const uint64_t MAX_VOTING_DURATION = 7 * DAY;

// default refresh period
const uint64_t REFRESH_PERIOD = 5 * MINUTE;

// minimum milestone duration
const uint64_t MIN_MILESTONE_DURATION = 1; // 14 * DAY;

// maximum milestone duration
const uint64_t MAX_MILESTONE_DURATION = 84 * DAY;

// minimum funding campaign duration
const uint64_t MIN_CAMPAIGN_DURATION = 1; // 14 * DAY;

// maximum funding campaign duration
const uint64_t MAX_CAMPAIGN_DURATION = 56 * DAY;

// todo this should be an argument in extend action
// and be saved inside a milestone/vote in question
const uint64_t TIMET = 14 * DAY; 
