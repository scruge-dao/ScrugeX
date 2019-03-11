
void scrugex::take(name eosAccount, uint64_t campaignId) {
  require_auth(eosAccount);
	_send(eosAccount, campaignId);
  
} // void scrugex::take


void scrugex::send(name eosAccount, uint64_t campaignId) {
	require_auth(_self);
	
	// fetch campaign
	campaigns_i campaigns(_self, _self.value);
	auto campaignItem = campaigns.find(campaignId);
	eosio_assert(campaignItem != campaigns.end(), "campaign does not exist");
	auto scope = campaignItem->campaignId;
	
	// check sell orders
	sellorders_i sellorders(_self, campaignId);
	auto notAttemptedSellOrders = sellorders.get_index<"byap"_n>();
  for (auto& orderItem : notAttemptedSellOrders) {
    
    if (orderItem.eosAccount != eosAccount) { // to-do smart sorting  
      continue;
    }
    
    if (orderItem.isPaid) { continue; }
    
    auto item = sellorders.find(orderItem.key);
    sellorders.modify(orderItem, same_payer, [&](auto& r) {
      r.attemptedPayment = true;
      r.isPaid = true;
    });
    
    // send back unused tokens
    if (orderItem.received.amount > 0) {
      _transfer(orderItem.eosAccount, orderItem.received, "ScrugeX: Tokens Sold on Exchange");
    }
    
    _scheduleSend(eosAccount, campaignId); 
    return;
	}
	
	// check buy orders
	
	buyorders_i buyorders(_self, campaignId);
	auto notAttemptedBuyOrders = buyorders.get_index<"byap"_n>();
  for (auto& orderItem : notAttemptedBuyOrders) {
    
    if (orderItem.eosAccount != eosAccount) { // to-do smart sorting 
      continue;
    }
    
    if (orderItem.isPaid) { continue; }
    
    auto item = buyorders.find(orderItem.key);
    buyorders.modify(item, same_payer, [&](auto& r) {
      r.attemptedPayment = true;
      r.isPaid = true;
    });
    
    asset diff = asset(orderItem.sum.amount - (uint64_t) ceil(orderItem.spent), orderItem.sum.symbol);
    if (diff.amount > 0 && orderItem.paymentReceived) {
      _transfer(orderItem.eosAccount, diff, "ScrugeX: Tokens Purchased on Exchange");
    }
   
    _scheduleSend(eosAccount, campaignId); 
    return;
	}
	
	// if refunding (both not reached soft cap or milestone vote failed)
	if (campaignItem->status == Status::refunding || campaignItem->status == Status::distributing) {
		
		// get contribution
		contributions_i contributions(_self, scope);
		auto contributionItem = contributions.find(eosAccount.value);
		
		eosio_assert(contributionItem->isPaid == false, "this user has already been paid");
		
		// if refunding (both not reached soft cap or milestone vote failed)
    if (campaignItem->status == Status::refunding) {
		  uint64_t refundPercent = 100 - campaignItem->releasedPercent;
      uint64_t paymentAmount = get_percent(contributionItem->quantity.amount, refundPercent);
      _transfer(eosAccount, asset(paymentAmount, campaignItem->hardCap.symbol), "ScrugeX: Refund for campaign");
    }
    
    // if campaign is over and tokens are being distributed to backers
    else {
      uint64_t paymentAmount = (uint64_t) floor((double)campaignItem->supplyForSale.amount /
          (double)campaignItem->raised.amount * (double)contributionItem->quantity.amount);
          
      auto paymentQuantity = asset(paymentAmount, campaignItem->supplyForSale.symbol);
      _transfer(eosAccount, paymentQuantity, "ScrugeX: Tokens Distribution", campaignItem->tokenContract);
      
      // also distribute exchange fund 
      exchangeinfo_i exchangeinfo(_self, campaignItem->campaignId);
			auto exchangeItem = exchangeinfo.begin();
			auto fund = exchangeItem->investorsFund;
			
			uint64_t fundPaymentAmount = (uint64_t) floor((double)exchangeItem->investorsFund.amount /
          (double)campaignItem->raised.amount * (double)contributionItem->quantity.amount); 
      
      if (fundPaymentAmount > 0) {
        auto fundPaymentQuantity = asset(fundPaymentAmount, exchangeItem->investorsFund.symbol);
        _transfer(eosAccount, fundPaymentQuantity, "ScrugeX: Exchange Fund Payment");
      }
    }
		
		contributions.modify(contributionItem, same_payer, [&](auto& r) {
			 r.attemptedPayment = true;
			 r.isPaid = true;
		});
	}
	
	// if returning excess funds (over hard cap)
	else {
		
		// get first in excessfunds
		excessfunds_i excessfunds(_self, scope);
		auto excessfundsItem = excessfunds.find(eosAccount.value);
		
		eosio_assert(excessfundsItem->isPaid == false, "this user has already been paid");

		_transfer(eosAccount, excessfundsItem->quantity, "ScrugeX: Excessive Funding Return");
		
		excessfunds.modify(excessfundsItem, same_payer, [&](auto& r) {
			 r.attemptedPayment = true;
			 r.isPaid = true;
		});
	}
	
} // void scrugex::send


void scrugex::pay(uint64_t campaignId) {
	require_auth(_self);
	
	// fetch campaign
	campaigns_i campaigns(_self, _self.value);
	auto campaignItem = campaigns.find(campaignId);
	eosio_assert(campaignItem != campaigns.end(), "campaign does not exist");
	auto scope = campaignItem->campaignId;

	// check sell orders
	sellorders_i sellorders(_self, campaignId);
	auto notAttemptedSellOrders = sellorders.get_index<"byap"_n>();
  
  for (auto& orderItem : notAttemptedSellOrders) {
    
    if (orderItem.attemptedPayment || orderItem.isPaid) {
      break;
    }
    
    auto item = sellorders.find(orderItem.key);
    sellorders.modify(orderItem, same_payer, [&](auto& r) {
      r.attemptedPayment = true;
    });
    
    _scheduleSend(orderItem.eosAccount, campaignId);
    _schedulePay(campaignId);
    
    return;
	}
	
	// check buy orders
	
	buyorders_i buyorders(_self, campaignId);
	auto notAttemptedBuyOrders = buyorders.get_index<"byap"_n>();
	
  for (auto& orderItem : notAttemptedBuyOrders) {
    
    if (!orderItem.paymentReceived) { continue; }
    
    if (orderItem.attemptedPayment || orderItem.isPaid) {
      break;
    }
    
    auto item = buyorders.find(orderItem.key);
    buyorders.modify(item, same_payer, [&](auto& r) {
      r.attemptedPayment = true;
    });
    
    _scheduleSend(orderItem.eosAccount, campaignId);
    _schedulePay(campaignId);
    
    return;
	}
	
	// check other payments
	
// 	eosio_assert(campaignItem->status == Status::refunding || 
// 							campaignItem->status == Status::distributing ||
// 							campaignItem->status == Status::excessReturning,
// 			"this campaign is not paying anyone right now");
	
	// if refunding (both not reached soft cap or milestone vote failed)
	// or if campaign is over and tokens are distributing to buyers
	if (campaignItem->status == Status::refunding || campaignItem->status == Status::distributing) {
		
		// get first in contributions
		contributions_i contributions(_self, scope);
		auto notAttemptedContributions = contributions.get_index<"byap"_n>();
		auto item = notAttemptedContributions.find(0);
		
		// if doesn't exist, close campaign, go back to refresh cycle
		if (item != notAttemptedContributions.end()) {
		  
			// set attempted payment
			auto eosAccount = item->eosAccount;
			auto contributionItem = contributions.find(eosAccount.value);
			contributions.modify(contributionItem, same_payer, [&](auto& r) {
				r.attemptedPayment = true;
			});
			
			// schedule payment and repeat 
			_scheduleSend(eosAccount, campaignId);
			_schedulePay(campaignId);
		}
		else {
			
			// no more payments weren't attempted, the rest can do it manually
			campaigns.modify(campaignItem, same_payer, [&](auto& r) {
				r.active = false;
			});
			
			// schedule refresh and exit (to not schedule next payout)
			_scheduleRefresh(1);
		}
	}
	
	// if returning excess funds (over hard cap)
	else if (campaignItem->status == Status::excessReturning) {
		
		 // get first in excessfunds
		excessfunds_i excessfunds(_self, scope);
		auto notAttemptedReturns = excessfunds.get_index<"byap"_n>();
		auto item = notAttemptedReturns.find(0);
		
		// if doesn't exist, close campaign, go back to refresh cycle
		if (item != notAttemptedReturns.end()) {
			// set attempted payment
			auto eosAccount = item->eosAccount;
			auto excessFundsItem = excessfunds.find(eosAccount.value);
			excessfunds.modify(excessFundsItem, same_payer, [&](auto& r) {
				r.attemptedPayment = true;
			});
			
			// schedule payment and repeat 
			_scheduleSend(eosAccount, campaignId);
			_schedulePay(campaignId);

		}
		else {
			
			// go back to funding state and refresh (and it will start milestones)
			campaigns.modify(campaignItem, same_payer, [&](auto& r) {
				r.status = Status::funding;
			});
			
			_scheduleRefresh(1);
		}
	}
	
} // void scrugex::startrefund
