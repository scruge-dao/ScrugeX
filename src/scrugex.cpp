#include "scrugex.hpp"
#include "refresh.hpp"
#include "debug.hpp"

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


void scrugex::newcampaign(name founderEosAccount, asset softCap, asset hardCap, 
		asset supplyForSale, name tokenContract, uint64_t initialFundsReleasePercent, bool kycEnabled,
		uint64_t maxUserContributionPercent, uint64_t minUserContributionPercent,
		uint64_t startTimestamp, uint64_t endTimestamp, vector<milestoneInfo> milestones) {

	require_auth(founderEosAccount);
	
	auto investmentSymbol = hardCap.symbol;
	
	// to-do validate arguments (make sure it's complete)
	eosio_assert(softCap < hardCap, "hard cap should be higher than soft cap");
	eosio_assert(startTimestamp < endTimestamp, "campaign end can not be earlier than campaign start");
	eosio_assert(milestones.size() > 0, "no milestones passed");
	
	eosio_assert(supplyForSale.symbol.is_valid(), "invalid supply for sale");
	eosio_assert(hardCap.symbol.is_valid(), "invalid hard cap");
	eosio_assert(softCap.symbol.is_valid(), "invalid soft cap");
	
	eosio_assert(hardCap.symbol == softCap.symbol, "cap symbols mismatch");
	eosio_assert(hardCap.symbol == EOS_SYMBOL, "only EOS can be used to receive investments");
	
	eosio_assert(initialFundsReleasePercent < 50,
		"initial funds release can not be higher than 50%");

	campaigns_i campaigns(_self, _self.value);
  auto campaignId = campaigns.available_primary_key();

	campaigns.emplace(_self, [&](auto& r) {
		r.campaignId = campaignId;
		r.founderEosAccount = founderEosAccount;
		r.softCap = softCap;
		r.hardCap = hardCap;
		r.initialFundsReleasePercent = initialFundsReleasePercent;
		r.maxUserContributionPercent = maxUserContributionPercent;
		r.minUserContributionPercent = minUserContributionPercent;
		r.startTimestamp = startTimestamp;
		r.endTimestamp = endTimestamp;
		r.raised = asset(0, investmentSymbol);
		r.currentMilestone = 0;
		r.status = Status::funding;
		r.excessReturned = asset(0, investmentSymbol);
		r.kycEnabled = kycEnabled;
		r.releasedPercent = 0;
		r.supplyForSale = supplyForSale;
		r.tokenContract = tokenContract;
		r.tokensReceived = false;
		r.waitingEndTimestamp = 0;
		r.active = true;
	});
	
  exchangeinfo_i exchangeinfo(_self, campaignId);
  exchangeinfo.emplace(_self, [&](auto& r) {
    r.status = ExchangeStatus::inactive;
		r.previousPrice = 0;
		r.roundPrice = 0;
		r.roundSellVolume = asset(0, supplyForSale.symbol);
		r.investorsFund = asset(0, investmentSymbol);
		r.sellEndTimestamp = 0;
		r.priceTimestamp = 0;
	});

	int scope = _getCampaignsCount();
	_updateCampaignsCount(scope);

	milestones_i table(_self, scope);
	auto lastDeadline = endTimestamp;
	auto totalFundsRelease = initialFundsReleasePercent;

	for (auto milestone: milestones) {

		// to-do validate milestone arguments (make sure it's complete)
		eosio_assert(lastDeadline < milestone.deadline,
			"next milestone deadline should always come after previous");
			
		eosio_assert(milestone.deadline - lastDeadline > MIN_MILESTONE_DURATION,
		  "milestone should be longer than 14 days");

		lastDeadline = milestone.deadline;
		totalFundsRelease += milestone.fundsReleasePercent;

		eosio_assert(totalFundsRelease <= 100,
			"total funds release can not go over 100%");

		table.emplace(_self, [&](auto& r) {
			r.id = table.available_primary_key();
			r.deadline = milestone.deadline;
			r.fundsReleasePercent = milestone.fundsReleasePercent;
		});
	}

	eosio_assert(totalFundsRelease == 100, 
		"total funds release can be less than 100%");
		
} // void scrugex::newcampaign


void scrugex::vote(name eosAccount, uint64_t campaignId, bool vote) {
	require_auth(eosAccount);

  campaigns_i campaigns(_self, _self.value);
  auto campaignItem = campaigns.find(campaignId);
	eosio_assert(campaignItem != campaigns.end(), "campaign does not exist");

	eosio_assert(eosAccount != campaignItem->founderEosAccount,
		"campaign founder can not participate in the voting");

	eosio_assert(campaignItem->status == Status::activeVote, "voting is not currently held");

	// get userId from kyc or eosAccount
	uint64_t userId = _verify(eosAccount, campaignItem->kycEnabled);

	auto scope = campaignItem->campaignId;
	auto quantity = _getContributionQuantity(scope, userId);
	eosio_assert(quantity.amount > 0, "only investors can participate in the voting");

	voting_i voting(_self, scope);
	eosio_assert(voting.begin() != voting.end(), "voting is not currently held");

	auto votingItem = voting.begin();
	while (votingItem != voting.end()) {
		if (votingItem->active) { break; }
		votingItem++;
	}

	eosio_assert(votingItem != voting.end(), "voting is not currently held");

	voters_i voters(_self, scope);
	voters.emplace(_self, [&](auto& r) {
		r.vote = vote;
		r.userId = userId;
		r.voteId = votingItem->primary_key();
	});

	voting.modify(votingItem, same_payer, [&](auto& r) {
		r.voters += 1;
		r.positiveVotes += vote;
		
		r.votedWeight += quantity.amount;
		if (vote) {
		  r.positiveWeight += quantity.amount;
		}
	});
	
} // void scrugex::vote


void scrugex::extend(uint64_t campaignId) {
	campaigns_i campaigns(_self, _self.value);
	auto campaignItem = campaigns.find(campaignId);
	eosio_assert(campaignItem != campaigns.end(), "campaign does not exist");

	require_auth(campaignItem->founderEosAccount);
	
	_startvote(campaignId, VoteKind::extendDeadline);
	
} // void scrugex::extend


void scrugex::refund(uint64_t campaignId) {
  
	// fetch campaign
	campaigns_i campaigns(_self, _self.value);
	auto campaignItem = campaigns.find(campaignId);
	eosio_assert(campaignItem != campaigns.end(), "campaign does not exist");
	auto scope = campaignItem->campaignId;
	
	require_auth(campaignItem->founderEosAccount);
	_refund(campaignItem->campaignId);

} // void scrugex::refund



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


// exchange

void scrugex::buy(name eosAccount, uint64_t campaignId, asset quantity, asset sum) {
  require_auth(eosAccount);
  
	// fetch campaign
	campaigns_i campaigns(_self, _self.value);
	auto campaignItem = campaigns.find(campaignId);
	eosio_assert(campaignItem != campaigns.end(), "campaign does not exist");

  uint64_t userId = _verify(eosAccount, campaignItem->kycEnabled);
  
  exchangeinfo_i exchangeinfo(_self, campaignId);
  auto exchangeItem = exchangeinfo.begin();
  eosio_assert(exchangeItem != exchangeinfo.end(), "exchange does not exist");
  eosio_assert(exchangeItem->status == ExchangeStatus::buying, "exchange doesn't take buy orders right now");
  
  eosio_assert(quantity.symbol.is_valid(), "invalid quantity");
  eosio_assert(quantity.symbol == exchangeItem->roundSellVolume.symbol, "incorrect quantity symbol");
  
  eosio_assert(sum.symbol.is_valid(), "invalid price");
  eosio_assert(sum.symbol == exchangeItem->investorsFund.symbol, "incorrect price symbol");

  // to-do calculate the price correctly, check it!!!
  double price = (double)quantity.amount / (double)sum.amount;
  
  eosio_assert(price > 0, "token price calculated with arguments passed is too low");
  
  eosio_assert(price <= exchangeItem->roundPrice,
    "token price calculated with arguments can not be higher than current auction price");
  
  buyorders_i buyorders(_self, campaignId);
  buyorders.emplace(eosAccount, [&](auto& r) {
    r.milestoneId = exchangeItem->milestoneId;
    r.key = buyorders.available_primary_key();
    r.userId = userId;
    r.quantity = quantity;
    r.sum = sum;
    r.price = price;
    r.eosAccount = eosAccount;
    r.attemptedPayment = false;
    r.isPaid = false;
    r.paymentReceived = false;
    r.timestamp = time_ms();
    r.purchased = asset(0, quantity.symbol);
    r.spent = 0;
  });
  
} // void scrugex::buy


void scrugex::sell(name eosAccount, uint64_t campaignId, asset quantity) {
  require_auth(eosAccount);
  
	// fetch campaign
	campaigns_i campaigns(_self, _self.value);
	auto campaignItem = campaigns.find(campaignId);
	eosio_assert(campaignItem != campaigns.end(), "campaign does not exist");

  uint64_t userId = _verify(eosAccount, campaignItem->kycEnabled);
  
  // to-do check if backer
  
  exchangeinfo_i exchangeinfo(_self, campaignId);
  auto exchangeItem = exchangeinfo.begin();
  eosio_assert(exchangeItem != exchangeinfo.end(), "exchange does not exist");
  eosio_assert(exchangeItem->status == ExchangeStatus::selling, "exchange doesn't take sell orders right now");
  
  eosio_assert(quantity.symbol.is_valid(), "invalid quantity");
  eosio_assert(quantity.symbol == exchangeItem->roundSellVolume.symbol, "incorrect symbol");
  eosio_assert(quantity.amount > 0, "invalid quantity");
  
  contributions_i contributions(_self, campaignId);
  auto contributionItem = contributions.find(userId);
  
  eosio_assert(contributionItem != contributions.end(), "you need to be an investor to sell tokens");
  
  // how many tokens will this contribution allows me
  uint64_t paymentAmount = (uint64_t) floor((double)campaignItem->supplyForSale.amount /
          (double)campaignItem->raised.amount * (double)contributionItem->quantity.amount);
          
  eosio_assert(paymentAmount >= quantity.amount, "can not sell more tokens that you have");
  
  exchangeinfo.modify(exchangeItem, same_payer, [&](auto& r) {
    r.roundSellVolume += quantity;
  });
  
  // lower contribution amount 
  
  sellorders_i sellorders(_self, campaignId);
  sellorders.emplace(eosAccount, [&](auto& r) {
    r.milestoneId = exchangeItem->milestoneId;
    r.key = sellorders.available_primary_key();
    r.userId = userId;
    r.eosAccount = eosAccount;
    r.quantity = quantity;
    r.timestamp = time_ms();
    r.attemptedPayment = false;
    r.isPaid = false;
    r.received = asset(0, exchangeItem->investorsFund.symbol);
  });
  
} // void scrugex::sell

// dispatch

extern "C" {
	
	void apply(uint64_t receiver, uint64_t code, uint64_t action) {
		
		if (code == receiver) {
			switch (action) {
				EOSIO_DISPATCH_HELPER(scrugex,
						(newcampaign)(vote)(extend)(refresh)(send)(pay)(take)(refund)
            (buy)(sell) 
						(destroy))
			}
		}
		else if (action == "transfer"_n.value && code != receiver) {
			execute_action(name(receiver), name(code), &scrugex::transfer);
		}
	}
};