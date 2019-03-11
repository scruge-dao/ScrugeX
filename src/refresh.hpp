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
				break;
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
      if (exchangeItem->status == ExchangeStatus::selling && exchangeItem->sellEndTimestamp < now) {
      
        sellorders_i sellorders(_self, campaignItem.campaignId);
      
        //close exchange if no orders exist
        auto newStatus = sellorders.begin() == sellorders.end() ? 
            ExchangeStatus::inactive : ExchangeStatus::buying;
        
        exchangeinfo.modify(exchangeItem, same_payer, [&](auto& r) {
          r.status = newStatus;
        });
        
        break;
      }
      
      if (exchangeItem->status == ExchangeStatus::buying) {
        
        // lower auction price when needed
        if (exchangeItem->priceTimestamp + EXCHANGE_PRICE_PERIOD < now) {
          exchangeinfo.modify(exchangeItem, same_payer, [&](auto& r) {
            r.priceTimestamp = now;
            r.roundPrice /= 5; // to-do formula
          });
        }
        
        // check if should close because of price threshold
        
        buyorders_i buyorders(_self, campaignItem.campaignId);
        auto ordersByPrice = buyorders.get_index<"bypricedesc"_n>(); // to-do smart sort milestone -> price -> time
        
        auto sellVolume = exchangeItem->roundSellVolume.amount;
        double roundPrice = (double)exchangeItem->roundPrice;
        double pICO = (double)campaignItem.raised.amount / (double)campaignItem.supplyForSale.amount;
        
        vector<uint64_t> ids;
        
        for (auto& orderItem : ordersByPrice) {
          
          // process current exchange only
          if (orderItem.milestoneId != exchangeItem->milestoneId) {
            continue;
          }
          
          // skip unpaid orders
          if (!orderItem.paymentReceived) { continue; }
          
          if (orderItem.price < exchangeItem->roundPrice) {
            break;
          }
          
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
          nextRefreshTime = 0;
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
								
								auto newPrice = exchangeItem->previousPrice;
								if (newPrice == 0) {
								  double pICO = (double)campaignItem.raised.amount / (double)campaignItem.supplyForSale.amount;
								  newPrice = pICO * EXCHANGE_PRICE_MULTIPLIER;
								}
								
                exchangeinfo.modify(exchangeItem, same_payer, [&](auto& r) {
                  r.milestoneId = nextMilestoneId;
                  r.status = ExchangeStatus::selling;
                  r.sellEndTimestamp = now + EXCHANGE_SELL_DURATION;
                  r.previousPrice = r.roundPrice;
                  r.roundPrice = newPrice * EXCHANGE_PRICE_MULTIPLIER;
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