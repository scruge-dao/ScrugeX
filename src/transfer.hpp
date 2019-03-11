void scrugex::transfer(name from, name to, asset quantity, string memo) {
	if (to != _self) { return; }
	require_auth(from);

	// check transfer
	eosio_assert(quantity.symbol.is_valid(), "invalid quantity");
	eosio_assert(quantity.amount > 0, "only positive quantity allowed");
	
	eosio_assert(memo != "", "incorrectly formatted memo");
	eosio_assert(is_number(memo), "campaignId is a number");

	uint64_t time = time_ms();
	auto campaignId = stoull(memo);
	auto eosAccount = from;

	// fetch campaign
	campaigns_i campaigns(_self, _self.value);
	auto campaignItem = campaigns.find(campaignId);
	eosio_assert(campaignItem != campaigns.end(), "campaign does not exist");

	auto scope = campaignItem->campaignId;
	auto code = name(get_code());
	
	// founder is putting money in escrow
  if (eosAccount == campaignItem->founderEosAccount) {
    
    eosio_assert(campaignItem->tokensReceived == false, "you have already locked transferred tokens");
    eosio_assert(campaignItem->status == Status::funding, "campaign has already started");
    eosio_assert(code == campaignItem->tokenContract, "you have to use the contract specified");
    eosio_assert(quantity == campaignItem->supplyForSale, "you have to transfer specified amount for sale");
    
    campaigns.modify(campaignItem, same_payer, [&](auto& r) {
      r.tokensReceived = true;
    });
	} 
	
	// backer is investing
	else {
    
    // check token contract
    eosio_assert(code == "eosio.token"_n, "you have to use the system EOS token");
    
    eosio_assert(time > campaignItem->startTimestamp, "campaign has not started yet");
    
    if (time < campaignItem->endTimestamp) {
      // primary investment 
      
      eosio_assert(time < campaignItem->endTimestamp, "campaign has ended");
      eosio_assert(campaignItem->status == Status::funding, "campaign is not running");
      eosio_assert(campaignItem->tokensReceived == true, "campaign has not been supplied with tokens to sale");
    
      // get userId from kyc or eosAccount
      uint64_t userId = _verify(eosAccount, campaignItem->kycEnabled);
    	
    	// check if allowed to invest this amount
      asset previous = _getContributionQuantity(scope, userId);
      asset total = previous + quantity;
    	
      asset max = get_percent(campaignItem->hardCap, campaignItem->maxUserContributionPercent);
      asset min = get_percent(campaignItem->hardCap, campaignItem->minUserContributionPercent);
      eosio_assert(max > total, "you can not contribute this much");
      eosio_assert(min < total, "you can not contribute this little");
    
    	// upsert contribution
    	contributions_i contributions(_self, scope);
    	auto contributionItem = contributions.find(eosAccount.value);
    
    	// upsert
    	if (contributionItem != contributions.end()) {
    		auto inUse = contributionItem->userId == userId;
    		eosio_assert(inUse, "this eos account was used to contrubute by another scruge user");
    
    		contributions.modify(contributionItem, same_payer, [&](auto& r) {
    			r.quantity += quantity;
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
    			r.quantity = quantity;
    			
    			r.attemptedPayment = false;
    			r.isPaid = false;
    		});
    	}
    
    	// update raised in campaigns
    	campaigns.modify(campaignItem, same_payer, [&](auto& r) {
    		r.raised += quantity;
    	});
    }
    else {
      // paying for exchange buy order
      
      exchangeinfo_i exchangeinfo(_self, campaignId);
      auto exchangeItem = exchangeinfo.begin();
      eosio_assert(exchangeItem != exchangeinfo.end(), "exchange does not exist");
      eosio_assert(exchangeItem->status == ExchangeStatus::buying, "campaign is not running");
      
      bool didAccept = false;
      
      // to-do sort
      buyorders_i buyorders(_self, campaignId);
      for (auto& orderItem : buyorders) {
        if (orderItem.eosAccount != eosAccount || orderItem.sum != quantity || orderItem.paymentReceived) {
          continue;
        }
        
        didAccept = true;
        buyorders.modify(orderItem, same_payer, [&](auto& r) {
          r.paymentReceived = true;
          r.timestamp = time_ms();
        });
        break;
      }
      
      eosio_assert(didAccept, "to use exchange, create an order first with [buy] action and transfer the exact amount specified in the order");
    }
	}
	
} // void scrugex::transfer