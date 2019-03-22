// Copyright © Scruge 2019.
// This file is part of ScrugeX.
// Created by Yaroslav Erohin.

void scrugex::extend(uint64_t campaignId) {
  _assertPaused();
	campaigns_i campaigns(_self, _self.value);
	auto campaignItem = campaigns.find(campaignId);
	
	eosio_assert(campaignItem != campaigns.end(), "campaign does not exist");
	require_auth(campaignItem->founderEosAccount);
	
	_startvote(campaignId, VoteKind::extendDeadline);
	
} // void scrugex::extend

void scrugex::refund(uint64_t campaignId) {
  _assertPaused();
	campaigns_i campaigns(_self, _self.value);
	auto campaignItem = campaigns.find(campaignId);
	
	eosio_assert(campaignItem != campaigns.end(), "campaign does not exist");
	eosio_assert(campaignItem->tokensReceived, "you did not transfer any tokens to sale");
	require_auth(campaignItem->founderEosAccount);
	
	_refund(campaignItem->campaignId);

} // void scrugex::refund

void scrugex::pause(bool value) {
  require_auth(_self);
  
	information_i information(_self, _self.value);
	auto infoItem = information.begin();
	
	if (information.begin() == information.end()) {
	  information.emplace(_self, [&](auto& r) {
	    r.campaignsCount = 0;
	    r.isPaused = value;
	  });
	}
	else {
	  eosio_assert(infoItem->isPaused != value, "contract is already in this state");
	  information.modify(information.begin(), same_payer, [&](auto& r) {
	    r.isPaused = value;
	  });
	}
} // void pause

void scrugex::buyram() {
  require_auth(_self);
  
  information_i information(_self, _self.value);
  auto infoItem = information.begin();
  
  eosio_assert(infoItem != information.end(), "ram fund is empty");
  eosio_assert(infoItem->ramFund.amount > 0, "ram fund is empty");

  action(
		permission_level{ _self, "active"_n }, "eosio"_n, "buyram"_n,
		make_tuple(_self, _self, infoItem->ramFund)
	).send();
	
  information.modify(infoItem, same_payer, [&](auto& r) {
    r.ramFund = asset(0, EOS_SYMBOL);
  });
  
} // void buyram