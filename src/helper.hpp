void scrugex::_pay(uint64_t campaignId) {
	action(
		permission_level{ _self, "active"_n },
		_self, "pay"_n,
		make_tuple(campaignId)
	).send();
} // void _pay

void scrugex::_transfer(name account, asset quantity, string memo, name contract) {
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

  uint64_t startSum = campaignItem->raised.amount;
  uint64_t hardCap = campaignItem->hardCap.amount;
  uint64_t softCap = campaignItem->softCap.amount;
	
	contributions_i contributions(_self, campaignId);
	auto sortedContributions = contributions.get_index<"byamountdesc"_n>();
		
	// distribution algorithm pt. 2
  if (startSum > hardCap) {
  	
    uint64_t backersCount = campaignItem->backersCount;
    uint64_t e = 0;
    uint64_t i = 0;
	
    for (auto& item : sortedContributions) {
      uint64_t element = item.quantity.amount;
		  
      if (element * i + startSum < hardCap) {
        e = element;
  
        uint64_t k = 10000000;
        while (k > 0) {
          while (e * i + startSum < hardCap) {
            e += k;
          }
          e -= k;
          k /= 10;
        }
        break;
      }
      startSum -= element;
      i += 1;
    }
		
    uint64_t raised = campaignItem->raised.amount;
    uint64_t newRaised = campaignItem->raised.amount;
  	
  	// to-do loop sorted contributions again and break after dealing with all
    for (auto& item : contributions) {
  		if (item.quantity.amount < e) {
        continue;
      }
  		
      excessfunds_i excessfunds(_self, campaignId);
      auto contribution = contributions.find(item.eosAccount.value);
      uint64_t returnAmount = contribution->quantity.amount - e;
  		
  		excessfunds.emplace(_self, [&](auto& r) {
        r.attemptedPayment = false;
    		r.isPaid = false;
    		r.eosAccount = contribution->eosAccount;
    		r.quantity = asset(returnAmount, contribution->quantity.symbol);
  		});
  		contributions.modify(contribution, same_payer, [&](auto& r) {
  		  r.quantity = asset(e, r.quantity.symbol);
  		});
  		newRaised -= returnAmount;
    }
		
    campaigns.modify(campaignItem, same_payer, [&](auto& r) {
      r.raised = asset(newRaised, r.raised.symbol);
      r.excessReturned = asset(raised - newRaised, r.raised.symbol);
      r.status = Status::excessReturning;
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
	//     "this eos account is not associated with scruge account");
	
	// return accountItem->id;
}

	return eosAccount.value;
	
} // void _verify

void scrugex::_stopvote(uint64_t campaignId) {
	campaigns_i campaigns(_self, _self.value);
	auto campaignItem = campaigns.find(campaignId);
	eosio_assert(campaignItem != campaigns.end(), "campaign does not exist");
	eosio_assert(campaignItem->active, "campaign is no longer active");

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
	eosio_assert(campaignItem->active, "campaign is no longer active");

	// get current milestone
	milestones_i milestones(_self, campaignItem->campaignId);
	auto milestoneId = campaignItem->currentMilestone;

	// create a voting record
	voting_i voting(_self, campaignItem->campaignId);

	// check if this/other vote exists for this milestone
	auto voteId = milestoneId * 100 + kind;
	auto thisVote = voting.find(voteId);

	eosio_assert(thisVote == voting.end(), "this voting already exists");
	
	// check other votes
	auto now = time_ms();
	auto end = now + VOTING_DURATION;

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


void scrugex::_startExchange(uint64_t campaignId, uint64_t nextMilestoneId) {
  exchangeinfo_i exchangeinfo(_self, campaignId);
	auto exchangeItem = exchangeinfo.begin();
	campaigns_i campaigns(_self, _self.value);
	auto campaignItem = campaigns.find(campaignId);
	
	auto newPrice = exchangeItem->previousPrice;
	if (newPrice == 0) {
	  double pICO = (double)campaignItem->raised.amount / (double)campaignItem->supplyForSale.amount;
	  newPrice = pICO * EXCHANGE_PRICE_MULTIPLIER;
	}
	
  exchangeinfo.modify(exchangeItem, same_payer, [&](auto& r) {
    r.milestoneId = nextMilestoneId;
    r.status = ExchangeStatus::selling;
    r.sellEndTimestamp = time_ms() + EXCHANGE_SELL_DURATION;
    r.previousPrice = r.roundPrice;
    r.roundPrice = newPrice * EXCHANGE_PRICE_MULTIPLIER;
    r.roundSellVolume = asset(0, r.roundSellVolume.symbol);
  });
  
} // void _startExchange

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
  _pay(campaignItem->campaignId);
	
} // void _refund

void scrugex::_updateCampaignsCount(uint64_t scope) {
	information_i information(_self, _self.value);
	
	auto lambda = [&](auto& row) {
		row.campaignsCount = scope + 1;
	};

	if (information.begin() == information.end()) {
		information.emplace(_self, lambda);
	}
	else { information.modify(information.begin(), same_payer, lambda); }
	
} // void _updateCampaignsCount

uint64_t scrugex::_getCampaignsCount() {
	information_i information(_self, _self.value);
	for (auto& item : information) {
		return item.campaignsCount;
	}
	return 0;
} // uint64_t _getCampaignsCount

double scrugex::_updatePrice(uint64_t campaignId) {
  
  auto now = time_ms();
  exchangeinfo_i exchangeinfo(_self, campaignId);
  auto exchangeItem = exchangeinfo.begin();
  
  // to-do calculate time
  double newPrice = exchangeItem->roundPrice / 5; // to-do formula
  
  if (exchangeItem->priceTimestamp + EXCHANGE_PRICE_PERIOD < now) {
    exchangeinfo.modify(exchangeItem, same_payer, [&](auto& r) {
      r.priceTimestamp = now;
      r.roundPrice = newPrice;
    });
  }
  return newPrice;
}

// to-do optimize
asset scrugex::_getContributionQuantity(uint64_t scope, uint64_t userId) {
	contributions_i contributions(_self, scope);
	asset total = asset(0, EOS_SYMBOL); // use investment symbol
	for (auto& item : contributions) {
		if (item.userId == userId) {
			 total += item.quantity;
		}
	}
	return total;
} // _getContributionQuantity
