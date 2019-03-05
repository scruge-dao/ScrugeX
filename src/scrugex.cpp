#include "scrugex.hpp"


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
      
      buyorders_i buyorders(_self, campaignId);
      auto buyOrderItem = buyorders.find(eosAccount.value);
      eosio_assert(buyOrderItem != buyorders.end(), "to use exchange, create an order first with [buy] action");
      
      eosio_assert(buyOrderItem->sum == quantity, "you have to pay the exact sum you specified in your order");
      
      buyorders.modify(buyOrderItem, same_payer, [&](auto& r) {
        r.paymentReceived = true;
        r.timestamp = time_ms();
      });
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
		r.previousPrice = asset(0, investmentSymbol);
		r.roundPrice = asset(0, investmentSymbol);
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

// to-do refactor
void scrugex::refresh() {
	require_auth(_self);

	auto now = time_ms();
	campaigns_i campaigns(_self, _self.value);
	
	// refresh again in x minutes if nothing else turns up
	uint64_t nextRefreshTime = REFRESH_PERIOD;

	for (auto& campaignItem: campaigns) {

		if (now < campaignItem.endTimestamp || campaignItem.active == false) {
			continue;
		}
		
		// funding is complete
		if (campaignItem.status == Status::funding) {
			
			// did not reach soft cap, refund all money
			if (campaignItem.raised < campaignItem.softCap) {
			
				_refund(campaignItem.campaignId);

				// don't schedule refresh if a campaign is refunding
				nextRefreshTime = 0;
			}
			else if (campaignItem.raised > campaignItem.hardCap) {  
				
				if (_willRefundExcessiveFunds(campaignItem.campaignId)) {
					// payout process is launched
					nextRefreshTime = 0;
				}
				else {
					// wait for next refresh cycle to continue setup below
					nextRefreshTime = 1;
				}

				break;
			}
			
			// release initial funds
			auto quantity = get_percent(campaignItem.raised, campaignItem.initialFundsReleasePercent);
			_transfer(campaignItem.founderEosAccount, quantity, "ScrugeX: Initial Funds");

			// start milestones
			campaigns.modify(campaignItem, same_payer, [&](auto& r) {
				r.releasedPercent += campaignItem.initialFundsReleasePercent;
				r.status = Status::milestone;
			});
			
			nextRefreshTime = 1;
			break;
		}
		
		// check if waiting time has passed
		if (campaignItem.status == Status::waiting && campaignItem.waitingEndTimestamp < now) {
			// founder failed to act, refunding
			_refund(campaignItem.campaignId);
			nextRefreshTime = 0;
			break;
		}

		// check if supposed to be refunding
		if (campaignItem.status == Status::refunding || 
				campaignItem.status == Status::distributing ||
				campaignItem.status == Status::excessReturning) {

			_schedulePay(campaignItem.campaignId);
			nextRefreshTime = 0;
			continue;
		}

    // check exchange
    
    exchangeinfo_i exchangeinfo(_self, campaignItem.campaignId);
    auto exchangeItem = exchangeinfo.begin();
    
    if (exchangeItem->status != ExchangeStatus::inactive) {
      
      // exchange sell period is over 
      if (exchangeItem->status == ExchangeStatus::buying && exchangeItem->sellEndTimestamp > now) {
      
        sellorders_i sellorders(_self, campaignItem.campaignId);
      
        //close exchange if no orders exist
        auto newStatus = sellorders.begin() == sellorders.end() ? 
            ExchangeStatus::inactive : ExchangeStatus::buying;
        
        exchangeinfo.modify(exchangeItem, same_payer, [&](auto& r) {
          r.status = newStatus;
        });
        
        break;
      }
      
      if (exchangeItem->status == ExchangeStatus::selling) {
        
        // lower auction price when needed
        if (exchangeItem->priceTimestamp + EXCHANGE_PRICE_PERIOD > now) {
          exchangeinfo.modify(exchangeItem, same_payer, [&](auto& r) {
            r.priceTimestamp = now;
            r.roundPrice /= 2;
          });
        }
        
        
        // check if should close because of price threshold
        
        bool volumeSurpassed = false;
        
        buyorders_i buyorders(_self, campaignItem.campaignId);
        auto ordersByPrice = buyorders.get_index<"bypricedesc"_n>();
        
        auto buyVolume = asset(0, exchangeItem->roundSellVolume.symbol);
        
        for (auto& orderItem : ordersByPrice) {
          buyVolume += orderItem.quantity;
          
          // calculate purchase percent
          
          if (buyVolume > exchangeItem->roundSellVolume) {
            volumeSurpassed = true;
            
            auto item = buyorders.find(orderItem.key);
            buyorders.modify(item, same_payer, [&](auto& r) {
              r.spent = buyVolume - exchangeItem->roundSellVolume;
            });
            
            break;
          }
          else {
            auto item = buyorders.find(orderItem.key);
            buyorders.modify(item, same_payer, [&](auto& r) {
              r.spent = r.sum;
            });
          }
        }
        
        // close deals
        if (volumeSurpassed) {
          
          // close exchange
          exchangeinfo.modify(exchangeItem, same_payer, [&](auto& r) {
            r.status = ExchangeStatus::inactive;
          });
        }
        
        break;
      }
    }
  
		// check for ongoing voting

		auto scope = campaignItem.campaignId;
		milestones_i milestones(_self, scope);
		auto milestoneId = campaignItem.currentMilestone;
		auto currentMilestoneItem = milestones.find(milestoneId);
		voting_i voting(_self, scope);

		for (auto& votingItem: voting) { 
			
			// to-do improve search for active voting
		
			// check extend deadline votings
			if (votingItem.milestoneId == milestoneId && votingItem.active &&
				votingItem.kind == VoteKind::extendDeadline) {

				// extend voting should be over  
				if (votingItem.endTimestamp < now || votingItem.voters == campaignItem.backersCount) {
					
					// stop it
					_stopvote(campaignItem.campaignId);

					// calculate decision
					if (votingItem.positiveWeight >= get_percent(votingItem.votedWeight, T1)) {

						// to-do test and complete

						// extend all subsequent deadlines
						for (auto& milestoneItem: milestones) {
							if (milestoneItem.deadline >= currentMilestoneItem->deadline) {
								milestones.modify(milestoneItem, same_payer, [&](auto& r) {
									r.deadline += TIMET;
								});
							}
						}

						// go back to milestone
						campaigns.modify(campaignItem, same_payer, [&](auto& r) {
							r.status = Status::milestone;
						});
						
						// to-do correct timers?
					}
				
					nextRefreshTime = 1;
				}

				break;
			}
		}

		if (currentMilestoneItem->deadline < now) {
			
			// will start voting unless told not to 
			bool shouldStartNextMilestoneVoting = true;
			
			// find ongoing milstone voting
			for (auto& votingItem: voting) { // to-do improve search for active voting
				if (votingItem.milestoneId == milestoneId && votingItem.active &&
					votingItem.kind == VoteKind::milestoneResult) {

					// vote has just ended, don't start another one
					shouldStartNextMilestoneVoting = false;
					
					// milestone voting should be over  
					if (votingItem.endTimestamp < now || votingItem.voters == campaignItem.backersCount) {
						
						// stop it
						_stopvote(campaignItem.campaignId);
						
						// calculate decision
						if (votingItem.positiveWeight >= get_percent(votingItem.votedWeight, T1)) {
							
							// milestone vote success
							
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
								
								// enable exchange 
								exchangeinfo_i exchangeinfo(_self, campaignItem.campaignId);
								auto exchangeItem = exchangeinfo.begin();
								
                exchangeinfo.modify(exchangeItem, same_payer, [&](auto& r) {
                  r.status = ExchangeStatus::selling;
                  r.sellEndTimestamp = time_ms() + EXCHANGE_SELL_DURATION;
                  r.priceTimestamp = time_ms();
                  r.previousPrice = r.roundPrice * EXCHANGE_PRICE_MULTIPLIER;
                  r.roundPrice = asset(0, r.roundPrice.symbol);
                  r.roundSellVolume = asset(0, r.roundSellVolume.symbol);
                });
  
							}
						}
						else {
							
							// milestone vote failed
							// wait for some time for founder to return funds or start extend voting
							campaigns.modify(campaignItem, same_payer, [&](auto& r) {
								r.status = Status::waiting;
								r.waitingEndTimestamp = now + WAITING_TIME;
							});
						}
						
						nextRefreshTime = 1;
					}
					
					break;
				}
			}

			if (shouldStartNextMilestoneVoting) {
				_startvote(campaignItem.campaignId, VoteKind::milestoneResult);
			}
		}
	}

	if (nextRefreshTime != 0) {
		_scheduleRefresh(nextRefreshTime);
	}
	
} // void scrugex::refresh


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
	
	// if refunding (both not reached soft cap or milestone vote failed)
	if (campaignItem->status == Status::refunding || campaignItem->status == Status::distributing) {
		
		// get contribution
		contributions_i contributions(_self, scope);
		auto contributionItem = contributions.find(eosAccount.value);
		
		eosio_assert(contributionItem->isPaid == false, "this user has already been paid");
		
		// if refunding (both not reached soft cap or milestone vote failed)
    if (campaignItem->status == Status::refunding) {
		  uint64_t refundPercent = 100 - campaignItem->releasedPercent;
      uint64_t paymentAmount	= get_percent(contributionItem->quantity.amount, refundPercent);
      _transfer(eosAccount, asset(paymentAmount, campaignItem->hardCap.symbol), "ScrugeX: Refund for campaign");
    }
    
    // if campaign is over and tokens are being distributed to backers
    else {
      uint64_t paymentAmount = campaignItem->supplyForSale.amount *
          contributionItem->quantity.amount / campaignItem->raised.amount; // to-do CHECK FOR OVERFLOW
      auto paymentQuantity = asset(paymentAmount, campaignItem->supplyForSale.symbol);
      _transfer(eosAccount, paymentQuantity, "ScrugeX: Tokens Distribution", campaignItem->tokenContract);
      
      // also distribute exchange fund 
      exchangeinfo_i exchangeinfo(_self, campaignItem->campaignId);
			auto exchangeItem = exchangeinfo.begin();
			auto fund = exchangeItem->investorsFund;
			
			uint64_t fundPaymentAmount = exchangeItem->investorsFund.amount *
          contributionItem->quantity.amount / campaignItem->raised.amount; // to-do CHECK FOR OVERFLOW
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
	
	eosio_assert(campaignItem->status == Status::refunding || 
							campaignItem->status == Status::distributing ||
							campaignItem->status == Status::excessReturning,
			"this campaign is not paying anyone right now");
	
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
  
  // to-do check if backers, or what?
  
  exchangeinfo_i exchangeinfo(_self, campaignId);
  auto exchangeItem = exchangeinfo.begin();
  eosio_assert(exchangeItem != exchangeinfo.end(), "exchange does not exist");
  eosio_assert(exchangeItem->status == ExchangeStatus::buying, "exchange doesn't take buy orders right now");
  
  eosio_assert(quantity.symbol.is_valid(), "invalid quantity");
  eosio_assert(quantity.symbol == exchangeItem->roundSellVolume.symbol, "incorrect quantity symbol");
  
  eosio_assert(sum.symbol.is_valid(), "invalid price");
  eosio_assert(sum.symbol == exchangeItem->investorsFund.symbol, "incorrect price symbol");
  
  auto price = sum.amount / quantity.amount;
  eosio_assert(price > 0, "token price calculated with arguments passed is too low");
  
  buyorders_i buyorders(_self, campaignId);
  buyorders.emplace(eosAccount, [&](auto& r) {
    r.key = buyorders.available_primary_key();
    r.eosAccount = eosAccount;
    r.quantity = quantity;
    r.sum = sum;
    r.price = asset(price, sum.symbol);
    r.attemptedPayment = false;
    r.isPaid = false;
    r.paymentReceived = false;
    r.timestamp = time_ms();
    r.spent = asset(0, sum.symbol);
  });
  
} // void scrugex::buy


void scrugex::sell(name eosAccount, uint64_t campaignId, asset quantity) {
  require_auth(eosAccount);
  
  // to-do check if backer
  
  exchangeinfo_i exchangeinfo(_self, campaignId);
  auto exchangeItem = exchangeinfo.begin();
  eosio_assert(exchangeItem != exchangeinfo.end(), "exchange does not exist");
  eosio_assert(exchangeItem->status == ExchangeStatus::selling, "exchange doesn't take sell orders right now");
  
  eosio_assert(quantity.symbol.is_valid(), "invalid quantity");
  eosio_assert(quantity.symbol == exchangeItem->roundSellVolume.symbol, "incorrect symbol");
  
  exchangeinfo.modify(exchangeItem, same_payer, [&](auto& r) {
    r.roundSellVolume += quantity;
  });
  
  // lower contribution amount 
  
  sellorders_i sellorders(_self, campaignId);
  sellorders.emplace(eosAccount, [&](auto& r) {
    r.eosAccount = eosAccount;
    r.quantity = quantity;
    r.timestamp = time_ms();
    r.attemptedPayment = false;
    r.isPaid = false;
  });
  
} // void scrugex::sell

// debug


void scrugex::destroy() {
	require_auth(_self);
	uint64_t scopes = _getCampaignsCount();

	information_i table(_self, _self.value);
	auto item = table.begin();
	while(item != table.end()) { item = table.erase(item); }

	campaigns_i campaigns(_self, _self.value);
	auto ca_item = campaigns.begin();
	while(ca_item != campaigns.end()) { ca_item = campaigns.erase(ca_item); }

	for (int i=0; i < scopes; i++) {
		contributions_i contributions(_self, i);
		auto c_item = contributions.begin();
		while(c_item != contributions.end()) { c_item = contributions.erase(c_item); }

		voting_i voting(_self, i);
		auto item = voting.begin();
		while(item != voting.end()) { item = voting.erase(item); }

		milestones_i milestones(_self, i);
		auto m_item = milestones.begin();
		while(m_item != milestones.end()) { m_item = milestones.erase(m_item); }

		voters_i voters(_self, i);
		auto v_item = voters.begin();
		while(v_item != voters.end()) { v_item = voters.erase(v_item); }
		
		excessfunds_i excessfunds(_self, i);
		auto e_item = excessfunds.begin();
		while(e_item != excessfunds.end()) { e_item = excessfunds.erase(e_item); }
		
	  
		sellorders_i sellorders(_self, i);
		auto s_item = sellorders.begin();
		while(s_item != sellorders.end()) { s_item = sellorders.erase(s_item); }
		
		exchangeinfo_i exchangeinfo(_self, i);
		auto ex_item = exchangeinfo.begin();
		while(ex_item != exchangeinfo.end()) { ex_item = exchangeinfo.erase(ex_item); }
	
		buyorders_i buyorders(_self, i);
		auto b_item = buyorders.begin();
		while(b_item != buyorders.end()) { b_item = buyorders.erase(b_item); }
	}
	
} // void scrugex::destroy


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