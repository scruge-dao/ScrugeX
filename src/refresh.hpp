#define PASS return make_tuple(0, RefreshAction::pass);
#define SKIP PRINT_("SKIP") return make_tuple(0, RefreshAction::skip);
#define DONE_ PRINT_("DONE_") return make_tuple(0, RefreshAction::done);
#define DONE(x) PRINT("DONE", x) return make_tuple((x), RefreshAction::doneT);

#define INIT uint64_t t = 0, nextRefreshTime = REFRESH_PERIOD; RefreshAction action;
#define R_CHECK(x) tie(t, action) = (x)(campaignItem, campaigns); if (action == RefreshAction::pass) {} else if (action == RefreshAction::doneT) { nextRefreshTime = t; break; } else if (action == RefreshAction::done) { break; } else if (action == RefreshAction::skip) { continue; }
#define V_CHECK(x) tie(t, action) = (x)(*votingItem, campaignItem, campaigns); if (action == RefreshAction::pass) {} else if (action == RefreshAction::doneT) { DONE(t) } else if (action == RefreshAction::done) { DONE_ } else if (action == RefreshAction::skip) { SKIP }
#define X_CHECK(x) tie(t, action) = (x)(campaignItem, campaigns); if (action == RefreshAction::pass) {} else if (action == RefreshAction::doneT) { DONE(t) } else if (action == RefreshAction::done) { DONE_ } else if (action == RefreshAction::skip) { SKIP }
#define param scrugex::param scrugex::

param _campaignFunding(const campaigns& campaignItem, campaigns_i& campaigns) {
  if (time_ms() < campaignItem.endTimestamp || campaignItem.active == false) {
		SKIP
	}
	PASS 
} // PARAM _campaignOver

param _initialRelease(const campaigns& campaignItem, campaigns_i& campaigns) {
  if (campaignItem.status == Status::funding) {
	
		// did not reach soft cap, refund all money
		if (campaignItem.raised < campaignItem.softCap) {
		
			_refund(campaignItem.campaignId);

			// don't schedule refresh if a campaign is refunding
			DONE(0)
		}
		else if (campaignItem.raised > campaignItem.hardCap) {  
			
			// if will refund, don't run refresh again
			bool willRefund = _willRefundExcessiveFunds(campaignItem.campaignId);
			DONE(willRefund ? 0 : 1)
		}
		
		// release initial funds
		auto quantity = get_percent(campaignItem.raised, campaignItem.initialFundsReleasePercent);
		_transfer(campaignItem.founderEosAccount, quantity, "ScrugeX: Initial Funds");

		// start milestones
		campaigns.modify(campaignItem, same_payer, [&](auto& r) {
			r.releasedPercent += campaignItem.initialFundsReleasePercent;
			r.status = Status::milestone;
		});
	
		DONE(1)
  }
	PASS
} // PARAM _fundingComplete

param _waiting(const campaigns& campaignItem, campaigns_i& campaigns) {
	if (campaignItem.status == Status::waiting && campaignItem.waitingEndTimestamp < time_ms()) {
		_refund(campaignItem.campaignId);
		DONE(1)
	}
	PASS 
} // PARAM _waitingOver

param _refunding(const campaigns& campaignItem, campaigns_i& campaigns) {
	if (campaignItem.status == Status::refunding || 
			campaignItem.status == Status::distributing ||
			campaignItem.status == Status::excessReturning) {
		_schedulePay(campaignItem.campaignId);
		SKIP
	}
	PASS
} // PARAM _isRefunding

// VOTING 

param _voting(const campaigns& campaignItem, campaigns_i& campaigns) {
  INIT
  
	voting_i voting(_self, campaignItem.campaignId);
  auto activeVoting = voting.get_index<"byactive"_n>();
  auto votingItem = activeVoting.begin();
  
  PRINT_("_extendDeadlineVoting")
  V_CHECK(_extendDeadlineVoting)
  
  PRINT_("_milestoneVoting")
	V_CHECK(_milestoneVoting)
	
	PASS
} // PARAM _voting

param _extendDeadlineVoting(const voting& votingItem, const campaigns& campaignItem, campaigns_i& campaigns) {
	auto now = time_ms();
	milestones_i milestones(_self, campaignItem.campaignId);
	auto milestoneId = campaignItem.currentMilestone;
	auto currentMilestoneItem = milestones.find(milestoneId);
	
	if (!votingItem.active ||
    votingItem.milestoneId != milestoneId ||
    votingItem.kind != VoteKind::extendDeadline) {
    
    PASS
  }

	// extend voting should be over
	if (votingItem.endTimestamp > now && votingItem.voters != campaignItem.backersCount) {
    PASS
	}
		
	// stop it
	_stopvote(campaignItem.campaignId);

	// calculate decision
	if (votingItem.positiveWeight >= get_percent(votingItem.votedWeight, T1)) {

		// extend this milestone duration
		milestones.modify(currentMilestoneItem, same_payer, [&](auto& r) {
			r.duration *= 1.25;
		});

		// go back to milestone
		campaigns.modify(campaignItem, same_payer, [&](auto& r) {
			r.status = Status::milestone;
		});
		
		// to-do correct timers?
	}

	DONE(1)
  
} // PARAM _extendDeadlineVoting

param _milestoneVoting(const voting& votingItem, const campaigns& campaignItem, campaigns_i& campaigns) {
  auto now = time_ms();
	milestones_i milestones(_self, campaignItem.campaignId);
	auto milestoneId = campaignItem.currentMilestone;
	auto currentMilestoneItem = milestones.find(milestoneId);
	
  // milestone is not over yet
  if (currentMilestoneItem->duration + currentMilestoneItem->startTimestamp > now) {
    PASS
  }
	
	// voting is not active
  if (!votingItem.active ||
      votingItem.milestoneId != milestoneId ||
      votingItem.kind != VoteKind::milestoneResult) {
    
    _startvote(campaignItem.campaignId, VoteKind::milestoneResult);
    DONE(1)
  }
	
	// milestone voting is not over yet
	if (votingItem.endTimestamp > now && votingItem.voters != campaignItem.backersCount) {
	  PASS  
	}
	
	// milestone voting complete
	_stopvote(campaignItem.campaignId);
	
	// if failed, change state and return 
	if (votingItem.positiveWeight < get_percent(votingItem.votedWeight, T1)) {
	  
		// milestone vote failed
		// wait for some time for founder to return funds or start extend voting
		campaigns.modify(campaignItem, same_payer, [&](auto& r) {
			r.status = Status::waiting;
			r.waitingEndTimestamp = now + WAITING_TIME;
		});
		
		DONE(1)
	}
		
	// milestone voting success 
	
	// release this part of funds 
	auto percent = get_percent(campaignItem.raised.amount, 
								 currentMilestoneItem->fundsReleasePercent);
	auto quantity = campaignItem.raised;
	quantity.amount = percent;
	_transfer(campaignItem.founderEosAccount, quantity, "ScrugeX: Milestone Payment");
	
	campaigns.modify(campaignItem, same_payer, [&](auto& r) {
		r.releasedPercent += currentMilestoneItem->fundsReleasePercent;
	});

	// get next milestone
	uint64_t nextMilestoneId = campaignItem.currentMilestone + 1;
	auto nextMilestoneItem = milestones.find(nextMilestoneId);

	if (nextMilestoneItem == milestones.end()) {

		// no more milestones
		// start distributing tokens
		campaigns.modify(campaignItem, same_payer, [&](auto& r) {
			r.status = Status::distributing;
		});
		
		_schedulePay(campaignItem.campaignId);
	}
	else {
		
		// switch milestone
		campaigns.modify(campaignItem, same_payer, [&](auto& r) {
			r.currentMilestone = nextMilestoneId;
			r.status = Status::milestone;
		});
		
		milestones.modify(nextMilestoneItem, same_payer, [&](auto& r) {
		  r.startTimestamp = now;
		});
		
		// enable exchange 
		_startExchange(campaignItem.campaignId, nextMilestoneId);
	}
	
  DONE(1)
	
} // PARAM _milestoneVoting

// EXCHANGE

param _runExchange(const campaigns& campaignItem, campaigns_i& campaigns) {
  INIT

  exchangeinfo_i exchangeinfo(_self, campaignItem.campaignId);
  auto exchangeItem = exchangeinfo.begin();
  
  if (exchangeItem->status != ExchangeStatus::inactive) {
    
    PRINT_("_closeSell")
    X_CHECK(_closeSell)
    
    PRINT_("_canClose")
    X_CHECK(_canClose)
  }
  
  PASS

} // PARAM _runExchange

param _closeSell(const campaigns& campaignItem, campaigns_i& campaigns) {
  exchangeinfo_i exchangeinfo(_self, campaignItem.campaignId);
  auto exchangeItem = exchangeinfo.begin();
  
  if (exchangeItem->status == ExchangeStatus::selling && exchangeItem->sellEndTimestamp < time_ms()) {
    
    PRINT_("exchange: sell phase ended")
    
    sellorders_i sellorders(_self, campaignItem.campaignId);
    
    auto newStatus = sellorders.begin() == sellorders.end() ? 
      ExchangeStatus::inactive : ExchangeStatus::buying;
      
    exchangeinfo.modify(exchangeItem, same_payer, [&](auto& r) {
      r.status = newStatus;
    });
    
    DONE_
  }
  PASS 
} // PARAM _sellOver

param _canClose(const campaigns& campaignItem, campaigns_i& campaigns) {
  _updatePrice(campaignItem.campaignId);
  
  exchangeinfo_i exchangeinfo(_self, campaignItem.campaignId);
  auto exchangeItem = exchangeinfo.begin();
  
  if (exchangeItem->status == ExchangeStatus::buying) {
    uint64_t now = time_ms();
    
    buyorders_i buyorders(_self, campaignItem.campaignId);
    auto ordersByPrice = buyorders.get_index<"specialindex"_n>();
    
    auto sellVolume = exchangeItem->roundSellVolume.amount;
    double roundPrice = (double)exchangeItem->roundPrice;
    double pICO = (double)campaignItem.raised.amount / (double)campaignItem.supplyForSale.amount;
    
    vector<uint64_t> ids;
    for (auto& orderItem : ordersByPrice) {
      
      // process current exchange only
      if (orderItem.milestoneId != exchangeItem->milestoneId) { continue; }
      
      // skip unpaid orders
      if (!orderItem.paymentReceived) { continue; }
      
      // elements are sorted, so no point to look further
      if (orderItem.price < exchangeItem->roundPrice) { break; }
      
      uint64_t purchaseAmount = min(orderItem.quantity.amount, sellVolume);
      sellVolume -= purchaseAmount;
      
      if (purchaseAmount > 0) {
        
        ids.push_back(orderItem.key);

        double cost = (double)purchaseAmount * roundPrice;
        
        auto item = buyorders.find(orderItem.key);
        buyorders.modify(item, same_payer, [&](auto& r) {
          r.purchased = asset(purchaseAmount, r.purchased.symbol);
          r.spent = cost;
        });
      }
      else {
        auto item = buyorders.find(orderItem.key);
        buyorders.modify(item, same_payer, [&](auto& r) {
          r.purchased = asset(0, r.purchased.symbol);
          r.spent = 0;
        });
      }
    }
    
    // closing exchange
    if (sellVolume == 0) {
      
      PRINT_("exchange: exchange closed")
      
      for (auto& id : ids) {
        auto orderItem = buyorders.find(id);
        
        contributions_i contributions(_self, campaignItem.campaignId);
        auto contributionItem = contributions.find(orderItem->userId);
        auto spent = (uint64_t) floor(orderItem->spent);
        
        if (contributionItem == contributions.end()) {
          campaigns.modify(campaignItem, same_payer, [&](auto& r) {
            r.backersCount += 1;
          });
          
          contributions.emplace(_self, [&](auto& r) {
            r.userId = orderItem->userId;
        		r.eosAccount = orderItem->eosAccount;
        		r.quantity = asset(spent, orderItem->sum.symbol);
        		r.attemptedPayment = false;
        		r.isPaid = false;
          });
        }
        else {
          contributions.modify(contributionItem, same_payer, [&](auto& r) {
            r.quantity -= asset(spent, r.quantity.symbol);
          });
        }
      }
      
      sellorders_i sellorders(_self, campaignItem.campaignId);
      for (auto& orderItem : sellorders) {
        
        // process this exchange only
        if (orderItem.milestoneId != exchangeItem->milestoneId) {
          continue;
        }
        
        contributions_i contributions(_self, campaignItem.campaignId);
        auto contributionItem = contributions.find(orderItem.userId);
        
        uint64_t cost = (uint64_t) floor((double)orderItem.quantity.amount * min(roundPrice, pICO));
        sellorders.modify(orderItem, same_payer, [&](auto& r) {
          r.received = asset(cost, r.received.symbol);
        });
        
        if (contributionItem->quantity.amount == cost) {
          campaigns.modify(campaignItem, same_payer, [&](auto& r) {
            r.backersCount -= 1;
          });
          
          contributions.erase(contributionItem);
        }
        else {
          contributions.modify(contributionItem, same_payer, [&](auto& r) {
            r.quantity -= asset(cost, r.quantity.symbol);
          });
        }
      }
      
      // close exchange
      
      double diff = roundPrice - pICO;
      uint64_t fundAmount = (uint64_t) floor((double)exchangeItem->roundSellVolume.amount * diff);
        
      exchangeinfo.modify(exchangeItem, same_payer, [&](auto& r) {
        r.investorsFund += asset(fundAmount, r.investorsFund.symbol);
        r.status = ExchangeStatus::inactive;
      });
      
      _schedulePay(campaignItem.campaignId);
      
      DONE(0)
    }
    
    PASS
  }
  
  PASS
  
} // PARAM _canClose

void scrugex::refresh() {
	require_auth(_self);
	
	INIT
	
	campaigns_i campaigns(_self, _self.value);
	for (auto& campaignItem: campaigns) {

    PRINT_("_campaignFunding")
		R_CHECK(_campaignFunding)
		
		PRINT_("_initialRelease")
		R_CHECK(_initialRelease)
		
		PRINT_("_waiting")
		R_CHECK(_waiting)

    PRINT_("_refunding")
    R_CHECK(_refunding)
    
    R_CHECK(_runExchange)
    
		R_CHECK(_voting)
	}

	if (nextRefreshTime != 0) {
		_scheduleRefresh(nextRefreshTime);
	}
	
} // void refresh
