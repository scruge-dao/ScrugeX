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
        
        // also distribute exchange fund 
        exchangeinfo_i exchangeinfo(_self, campaignItem.campaignId);
  			auto exchangeItem = exchangeinfo.begin();
  			auto fund = exchangeItem->investorsFund;
  			
  			uint64_t fundPaymentAmount = (uint64_t) floor((double)exchangeItem->investorsFund.amount /
            (double)campaignItem.raised.amount * (double)contributionItem->quantity.amount); 
        
        if (fundPaymentAmount > 0) {
          auto fundPaymentQuantity = asset(fundPaymentAmount, exchangeItem->investorsFund.symbol);
          _transfer(eosAccount, fundPaymentQuantity, "ScrugeX: Exchange Fund Payment");
        }
      }
  		
  		contributions.modify(contributionItem, same_payer, [&](auto& r) {
  			 r.attemptedPayment = true;
  			 r.isPaid = true;
  		});
  		
      _scheduleSend(eosAccount, campaignId);
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
    
    _scheduleSend(eosAccount, campaignId);
    return;
  }
	
	// get last complete exchange id 
	auto lastCompleteExchangeId = _getLastCompleteExchangeId(campaignId);
	
	// check sell orders
	sellorders_i sellorders(_self, campaignId);
	auto usersOrders = sellorders.get_index<"byuserid"_n>();
	auto sellIndex = sellorders.get_index<"byuserid"_n>();
  auto sellOrderItem = sellIndex.find(userId);
  
  while (sellOrderItem != sellIndex.end()) {
    if (!sellOrderItem->isPaid && lastCompleteExchangeId >= sellOrderItem->milestoneId) {
      const auto& orderItem = *sellOrderItem;
      
      if (orderItem.userId != userId) {
        break;
      }
    
      // auto item = sellorders.find(orderItem.key);
      sellIndex.modify(sellOrderItem, same_payer, [&](auto& r) {
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
    sellOrderItem++;
	}
	
	// check buy orders
	buyorders_i buyorders(_self, campaignId);
	auto notAttemptedBuyOrders = buyorders.get_index<"byuserid"_n>();
	auto buyIndex = buyorders.get_index<"byuserid"_n>();
  auto buyOrderItem = buyIndex.find(userId);
	
	while (buyOrderItem != buyIndex.end()) {
    if (!buyOrderItem->isPaid && buyOrderItem->paymentReceived &&
        lastCompleteExchangeId >= buyOrderItem->milestoneId) {
      const auto& orderItem = *buyOrderItem;
      
      if (orderItem.userId != userId) {
        break;
      }
    
      // auto item = buyorders.find(orderItem.key);
      buyIndex.modify(buyOrderItem, same_payer, [&](auto& r) {
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
    buyOrderItem++;
	}

} // void scrugex::send

void scrugex::pay(uint64_t campaignId) {
	require_auth(_self);
	
	// fetch campaign
	campaigns_i campaigns(_self, _self.value);
	const auto& campaignItem = campaigns.get(campaignId, "campaign does not exist");
	
	// check contributions
	if (campaignItem.status == Status::refunding || campaignItem.status == Status::distributing) {
		
		contributions_i contributions(_self, campaignId);
		auto contribIndex = contributions.get_index<"byuserid"_n>();
		auto contributionItem = contribIndex.begin();
		
		while (contributionItem != contribIndex.end()) {
      if (!contributionItem->attemptedPayment) {
        
        // ... 
        contribIndex.modify(contributionItem, same_payer, [&](auto& r) {
          r.attemptedPayment = true;
        });
        
        _scheduleSend(contributionItem->eosAccount, campaignId);
        _schedulePay(campaignId);
        return;
      }
		  contributionItem++;
		}
	}
	
	// check excess funds
	excessfunds_i excessfunds(_self, campaignId);
	auto excessIndex = excessfunds.get_index<"byeosaccount"_n>();
	auto excessItem = excessIndex.begin();
	
	if (excessItem != excessIndex.end() && !excessItem->attemptedPayment) {
    
    // set attempted payment
		auto eosAccount = excessItem->eosAccount;
		// auto excessFundsItem = excessfunds.find(eosAccount.value);
		excessIndex.modify(excessItem, same_payer, [&](auto& r) {
			r.attemptedPayment = true;
		});
		
		_scheduleSend(eosAccount, campaignId);
		_schedulePay(campaignId);
		return;
	}
	
	// get last complete exchange id
	auto lastCompleteExchangeId = _getLastCompleteExchangeId(campaignId);
	
	// no exchanges were complete yet, there is no exchange with id 0
	if (lastCompleteExchangeId == 0) { return; }
	
	// check sell orders
	sellorders_i sellorders(_self, campaignId);
	auto sellIndex = sellorders.get_index<"byuserid"_n>();
	auto sellOrderItem = sellIndex.begin();
  
  while (sellOrderItem != sellIndex.end()) {
    if (!sellOrderItem->attemptedPayment && lastCompleteExchangeId >= sellOrderItem->milestoneId) {
      
      // auto item = sellorders.find(sellOrderItem->key);
      sellIndex.modify(sellOrderItem, same_payer, [&](auto& r) {
        r.attemptedPayment = true;
      });
      
      _scheduleSend(sellOrderItem->eosAccount, campaignId);
      _schedulePay(campaignId);
      return;
    }
    sellOrderItem++;
  }
	
	// check buy orders
	buyorders_i buyorders(_self, campaignId);
	auto buyIndex = buyorders.get_index<"byuserid"_n>();
	auto buyOrderItem = buyIndex.begin();
	
	while (buyOrderItem != buyIndex.end()) {
    if (!buyOrderItem->attemptedPayment && !buyOrderItem->paymentReceived &&
        lastCompleteExchangeId >= buyOrderItem->milestoneId) {
      
      // auto item = buyorders.find(buyOrderItem->key);
      buyIndex.modify(buyOrderItem, same_payer, [&](auto& r) {
        r.attemptedPayment = true;
      });
      
      _scheduleSend(buyOrderItem->eosAccount, campaignId);
      _schedulePay(campaignId);
      return;
  	}
  	buyOrderItem++;
	}
	
} // void scrugex::startrefund