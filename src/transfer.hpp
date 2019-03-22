// Copyright © Scruge 2019.
// This file is part of ScrugeX.
// Created by Yaroslav Erohin.

void scrugex::transfer(name from, name to, asset quantity, string memo) {
	if (to != _self) { return; }
	
	_assertPaused();
	require_auth(from);

	// check transfer
	eosio_assert(quantity.symbol.is_valid(), "invalid quantity");
	eosio_assert(quantity.amount > 0, "only positive quantity allowed");
	
	eosio_assert(memo != "", "incorrectly formatted memo");
	eosio_assert(is_number(memo), "campaignId is a number");

	uint64_t now = time_ms();
	auto campaignId = stoull(memo);
	auto eosAccount = from;

	// fetch campaign
	campaigns_i campaigns(_self, _self.value);
	auto campaignItem = campaigns.find(campaignId);
	eosio_assert(campaignItem != campaigns.end(), "campaign does not exist");

	auto code = name(get_code());
	
	// founder is putting money in escrow
  if (eosAccount == campaignItem->founderEosAccount) {
    
    eosio_assert(campaignItem->tokensReceived == false, "you have already locked transferred tokens");
    eosio_assert(campaignItem->status == Status::funding, "campaign has already started");
    eosio_assert(code == campaignItem->tokenContract, "you have to use the contract specified");
    eosio_assert(quantity.symbol == campaignItem->supplyForSale.symbol, "supply symbol mismatch");
    eosio_assert(quantity == campaignItem->supplyForSale, "you have to transfer specified amount for sale");
    
    campaigns.modify(campaignItem, same_payer, [&](auto& r) {
      r.tokensReceived = true;
    });
	} 
	
	// backer is investing
	else {
    
    // check token contract
    eosio_assert(code == "eosio.token"_n, "you have to use the system EOS token");
    
    eosio_assert(now > campaignItem->startTimestamp, "campaign has not started yet");
    eosio_assert(campaignItem->tokensReceived, "campaign has not been supplied with tokens to sell");
    eosio_assert(now < campaignItem->endTimestamp, "campaign has ended");
    eosio_assert(campaignItem->status == Status::funding, "campaign is not running");
    
    // get userId from kyc or eosAccount
    uint64_t userId = _verify(eosAccount, campaignItem->kycEnabled);
    
    // check if allowed to invest this amount
    asset previous = _getContributionQuantity(campaignId, userId);
    asset total = previous + quantity;
    
    asset max = get_percent(campaignItem->hardCap, campaignItem->maxUserContributionPercent);
    eosio_assert(max >= total, "you can not contribute this much");
    eosio_assert(campaignItem->minUserContribution <= total, "you can not contribute such small amount");
    
    // upsert contribution
    contributions_i contributions(_self, campaignId);
    auto contributionItem = contributions.find(eosAccount.value);
    
    // take ram comission  
    auto investment = quantity;
    
    // only once
    if (previous.amount == 0) { 
      auto ramComission = _getRamPriceKB();
      investment -= ramComission;
      
      information_i information(_self, _self.value);
      auto infoItem = information.begin();
      information.modify(infoItem, same_payer, [&](auto& r) {
        r.ramFund += ramComission;
      });
    }
    
    // upsert
    if (contributionItem != contributions.end()) {
      auto inUse = contributionItem->userId == userId;
      eosio_assert(inUse, "this eos account was used to contrubute by another scruge user");
    
      contributions.modify(contributionItem, same_payer, [&](auto& r) {
        r.quantity += investment;
      });
    }
    else {
      // advance backers
      if (previous.amount == 0) {
        campaigns.modify(campaignItem, same_payer, [&](auto& r) {
          r.backersCount += 1;
        });
      }
      
      contributions.emplace(_self, [&](auto& r) {
        r.userId = userId;
        r.eosAccount = eosAccount;
        r.quantity = investment;
        
        r.attemptedPayment = false;
        r.isPaid = false;
      });
    }
  
  	// update raised in campaigns
    campaigns.modify(campaignItem, same_payer, [&](auto& r) {
      r.raised += investment;
    });
	}
	
} // void scrugex::transfer