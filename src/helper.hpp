void scrugex::_pay(uint64_t campaignId) {
	action(
		permission_level{ _self, "active"_n },
		_self, "pay"_n,
		make_tuple(campaignId)
	).send();
} // void _pay

void scrugex::_transfer(name account, asset quantity, string memo, name contract) {
  _assertPaused();
  
	action(
		permission_level{ _self, "active"_n },
		contract, "transfer"_n,
		make_tuple(_self, account, quantity, memo)
	).send();
} // void _transfer

void scrugex::_transfer(name account, asset quantity, string memo) {
	_transfer(account, quantity, memo, "eosio.token"_n);
} // void _transfer

void scrugex::_send(name eosAccount, uint64_t campaignId) {
  action(
		permission_level{ _self, "active"_n },
		_self, "send"_n,
		make_tuple(eosAccount, campaignId)
	).send();
} // void _send

void scrugex::_scheduleRefresh(uint64_t nextRefreshTime) {
	cancel_deferred("refresh"_n.value);
	
	transaction t{};
	t.actions.emplace_back(permission_level(_self, "active"_n),
									 _self, "refresh"_n,
									 make_tuple());
	t.delay_sec = nextRefreshTime;
	t.send("refresh"_n.value, _self, false);
	
} // void _scheduleRefresh

void scrugex::_schedulePay(uint64_t campaignId) {
	transaction t{};
	t.actions.emplace_back(permission_level(_self, "active"_n),
									 _self, "pay"_n,
									 make_tuple( campaignId ));
	t.delay_sec = 2;
	t.send(time_ms() + 1, _self, false);

} //void _schedulePay

void scrugex::_scheduleSend(name eosAccount, uint64_t campaignId) {
	auto now = time_ms();
	transaction t{};
	t.actions.emplace_back(permission_level(_self, "active"_n),
									 _self, "send"_n,
									 make_tuple( eosAccount, campaignId ));
	t.delay_sec = 1;
	t.send(time_ms(), _self, false);

} // void _scheduleSend

bool scrugex::_willRefundExcessiveFunds(uint64_t campaignId) {
	campaigns_i campaigns(_self, _self.value);
	auto campaignItem = campaigns.find(campaignId);
  eosio_assert(campaignItem != campaigns.end(), "campaign does not exist");

  auto startSum = campaignItem->raised;
  auto hardCap = campaignItem->hardCap;
  auto softCap = campaignItem->softCap;
	
	contributions_i contributions(_self, campaignId);
	auto sortedContributions = contributions.get_index<"byamountdesc"_n>();
		
	// distribution algorithm pt. 2
  if (startSum > hardCap) {
  	
    uint64_t backersCount = campaignItem->backersCount;
    asset e;
    uint64_t i = 0;
	
    for (auto& item : sortedContributions) {
      auto element = item.quantity;
		  
      if (element * i + startSum < hardCap) {
        e = element;
  
        uint64_t k = 10'000'000;
        while (k > 0) {
          auto ka = asset(k, e.symbol);
          while (e * i + startSum < hardCap) {
            e += ka;
          }
          e -= ka;
          k /= 10;
        }
        break;
      }
      startSum -= element;
      i += 1;
    }
		
    auto raised = campaignItem->raised;
    auto newRaised = campaignItem->raised;
  	
  	// to-do loop sorted contributions again and break after dealing with all
    for (auto& item : contributions) {
  		if (item.quantity < e) {
        continue;
      }
  		
      excessfunds_i excessfunds(_self, campaignId);
      auto contribution = contributions.find(item.eosAccount.value);
      auto returnAmount = contribution->quantity - e;
  		
  		excessfunds.emplace(_self, [&](auto& r) {
        r.attemptedPayment = false;
    		r.isPaid = false;
    		r.eosAccount = contribution->eosAccount;
    		r.quantity = returnAmount;
  		});
  		contributions.modify(contribution, same_payer, [&](auto& r) {
  		  r.quantity = e;
  		});
  		newRaised = newRaised - returnAmount;
    }
		
    campaigns.modify(campaignItem, same_payer, [&](auto& r) {
      r.raised = newRaised;
      r.excessReturned = raised - newRaised;
    });
  }
	
	// schedule payout
	_schedulePay(campaignId);
	
	return true;
	
} // void _refundHardCap

uint64_t scrugex::_verify(name eosAccount, bool kycEnabled) {
  if (kycEnabled) {
    // accounts_i accounts("scrugeverify"_n, _self.value);
    // auto accountItem = accounts.find(eosAccount);
    
    // eosio_assert(accountItem != accounts.end(), "this scruge account is not verified");
    // eosio_assert(accountItem->eosAccount == eosAccount,
    //   "this eos account is not associated with scruge account");
    
    // return accountItem->id;
  }
  
  return eosAccount.value;
    
} // void _verify

void scrugex::_stopvote(uint64_t campaignId) {
	campaigns_i campaigns(_self, _self.value);
	auto campaignItem = campaigns.find(campaignId);
	eosio_assert(campaignItem != campaigns.end(), "campaign does not exist");

	milestones_i milestones(_self, campaignItem->campaignId);
	auto milestoneId = campaignItem->currentMilestone;

	// create a voting record
	voting_i voting(_self, campaignItem->campaignId);
	
	auto votingItem = voting.begin();
	while (votingItem != voting.end()) {
		if (votingItem->active) { break; }
		votingItem++;
	}
	eosio_assert(votingItem != voting.end(), "voting is not currently held");

	voting.modify(votingItem, same_payer, [&](auto& r) {
		r.active = false;
	});
	
} // void _stopvote

void scrugex::_startvote(uint64_t campaignId, uint8_t kind) {
	campaigns_i campaigns(_self, _self.value);
	auto campaignItem = campaigns.find(campaignId);
	eosio_assert(campaignItem != campaigns.end(), "campaign does not exist");
	eosio_assert(campaignItem->status == Status::milestone, "milestone is not running");

	// get current milestone
	milestones_i milestones(_self, campaignItem->campaignId);
	auto milestoneId = campaignItem->currentMilestone;
	auto milestoneItem = milestones.find(milestoneId);

	// create a voting record
	voting_i voting(_self, campaignItem->campaignId);

	// check if this/other vote exists for this milestone
	auto voteId = milestoneId * 100 + kind;
	auto thisVote = voting.find(voteId);

	eosio_assert(thisVote == voting.end(), "this voting already exists");
	
	auto now = time_ms();
	auto duration = milestoneItem->duration / 10;
	duration = max(MIN_VOTING_DURATION, duration);
	duration = min(MAX_VOTING_DURATION, duration);
	auto end = now + duration;
	
	// delete voters from previous voting
	voters_i voters(_self, campaignItem->campaignId);
	auto v_item = voters.begin();
	while(v_item != voters.end()) { v_item = voters.erase(v_item); }

	voting.emplace(_self, [&](auto& r) {
		r.voteId = voteId;
		r.kind = kind;
		r.milestoneId = milestoneId;
		r.startTimestamp = now;
		r.endTimestamp = end;
		r.voters = 0;
		r.positiveVotes = 0;
		r.active = true;
		r.votedWeight = 0;
		r.positiveWeight = 0;
	});

	campaigns.modify(campaignItem, same_payer, [&](auto& r) {
		r.status = Status::activeVote;
	});
} // void _startvote

void scrugex::_refund(uint64_t campaignId) {
  campaigns_i campaigns(_self, _self.value);
  auto campaignItem = campaigns.find(campaignId);
  eosio_assert(campaignItem != campaigns.end(), "campaign does not exist");
  
  campaigns.modify(campaignItem, same_payer, [&](auto& r) {
    r.status = Status::refunding;
  });
  
  // return tokens to founder 
  _transfer(campaignItem->founderEosAccount, campaignItem->supplyForSale, 
      "ScrugeX: Tokens Return", campaignItem->tokenContract);
  
  // return money to investors
  _schedulePay(campaignItem->campaignId);
	
} // void _refund

asset scrugex::_getContributionQuantity(uint64_t scope, uint64_t userId) {
	contributions_i contributions(_self, scope);
  auto index = contributions.get_index<"byuserid"_n>();
  auto item = index.find(userId);

	asset total = asset(0, EOS_SYMBOL); // to-do replace with investment symbol
	while (item != index.end() && item->userId == userId) {
		if (item->userId == userId) {
			 total += item->quantity;
		}
		item++;
	}
	return total;
} // asset _getContributionQuantity

void scrugex::_assertPaused() {
  information_i information(_self, _self.value);
  auto infoItem = information.begin();
  if (information.begin() != information.end()) {
    eosio_assert(!infoItem->isPaused, "contract is paused");
  }
} // void _assertPaused

asset scrugex::_getRamPriceKB() {
  static constexpr symbol ramcore_symbol = symbol(symbol_code("RAMCORE"), 4);
  rammarket_i rammarket("eosio"_n, "eosio"_n.value);
  auto item = rammarket.begin();
  
  if (item == rammarket.end()) {
    return asset(10, EOS_SYMBOL); // this can only happen on testnet
  }
  
  auto priceB = (double)item->quote.balance.amount / ((double)item->base.balance.amount);
  auto amount = priceB * 1024;
  return asset(amount, EOS_SYMBOL);
  
} // void _getRamPrice