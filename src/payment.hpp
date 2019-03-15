void scrugex::take(name eosAccount, uint64_t campaignId) {
  require_auth(eosAccount);
	_send(eosAccount, campaignId);
  
} // void scrugex::take

void scrugex::send(name eosAccount, uint64_t campaignId) {
	require_auth(_self);
	
	// fetch campaign
	campaigns_i campaigns(_self, _self.value);
	const auto& campaignItem = campaigns.get(campaignId, "campaign does not exist");
	
	uint64_t userId = _verify(eosAccount, campaignItem.kycEnabled);
	
	// pay for contribution
	if (campaignItem.status == Status::refunding || campaignItem.status == Status::distributing) {
		
		contributions_i contributions(_self, campaignId);
		auto contributionItem = contributions.find(eosAccount.value);
		
		if (contributionItem != contributions.end() && !contributionItem->isPaid) {
  		
  		// if refunding (both not reached soft cap or milestone vote failed)
      if (campaignItem.status == Status::refunding) {
  		  uint64_t refundPercent = 100 - campaignItem.releasedPercent;
        uint64_t paymentAmount = get_percent(contributionItem->quantity.amount, refundPercent);
        _transfer(eosAccount, asset(paymentAmount, campaignItem.hardCap.symbol), "ScrugeX: Refund for campaign");
      }
      else {
        
        uint64_t paymentAmount = (uint64_t) floor((double)campaignItem.supplyForSale.amount /
            (double)campaignItem.raised.amount * (double)contributionItem->quantity.amount);
            
        auto paymentQuantity = asset(paymentAmount, campaignItem.supplyForSale.symbol);
        _transfer(eosAccount, paymentQuantity, "ScrugeX: Tokens Distribution", campaignItem.tokenContract);
      }
  		
  		contributions.modify(contributionItem, same_payer, [&](auto& r) {
  			 r.attemptedPayment = true;
  			 r.isPaid = true;
  		});
  		
      return;
  	}
	}
	
	// check excess funds
	excessfunds_i excessfunds(_self, campaignId);
	auto excessIndex = excessfunds.get_index<"byeosaccount"_n>();
	auto excessItem = excessIndex.find(eosAccount.value);
	
  if (excessItem != excessIndex.end() && !excessItem->isPaid) {
    _transfer(eosAccount, excessItem->quantity, "ScrugeX: Excessive Funding Return");
    
    excessIndex.modify(excessItem, same_payer, [&](auto& r) {
      r.attemptedPayment = true;
      r.isPaid = true;
    });
    
    return;
  }
  
  eosio_assert(false, "there is nothing to pay for");

} // void scrugex::send

void scrugex::pay(uint64_t campaignId) {
	require_auth(_self);
	
	// fetch campaign
	campaigns_i campaigns(_self, _self.value);
	const auto& campaignItem = campaigns.get(campaignId, "campaign does not exist");
	
	// check contributions
	if (campaignItem.status == Status::refunding || campaignItem.status == Status::distributing) {
		
		contributions_i contributions(_self, campaignId);
		auto contribIndex = contributions.get_index<"byap"_n>();
		auto contributionItem = contribIndex.find(0);
		
		if (contributionItem != contribIndex.end() && !contributionItem->attemptedPayment) {
      
      contribIndex.modify(contributionItem, same_payer, [&](auto& r) {
        r.attemptedPayment = true;
      });
      
      _scheduleSend(contributionItem->eosAccount, campaignId);
      _schedulePay(campaignId);
      return;
		}
	}
	
	// check excess funds
	excessfunds_i excessfunds(_self, campaignId);
	auto excessIndex = excessfunds.get_index<"byap"_n>();
	auto excessItem = excessIndex.find(0);
	
	if (excessItem != excessIndex.end() && !excessItem->attemptedPayment) {
    
    // set attempted payment
		auto eosAccount = excessItem->eosAccount;
		excessIndex.modify(excessItem, same_payer, [&](auto& r) {
			r.attemptedPayment = true;
		});
		
		_scheduleSend(eosAccount, campaignId);
		_schedulePay(campaignId);
		return;
	}
	
	eosio_assert(false, "no payment to make");

} // void scrugex::startrefund