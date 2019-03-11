#define CHECK(x) tie(t, action) = (x)(campaignItem, campaigns); if (action == RefreshAction::pass) {} else if (action == RefreshAction::doneT) { nextRefreshTime = t; break; } else if (action == RefreshAction::done) { break; } else if (action == RefreshAction::skip) { continue; }

void scrugex::refresh() {
	require_auth(_self);

	auto now = time_ms();
	campaigns_i campaigns(_self, _self.value);
	
	uint64_t t = 0, nextRefreshTime = REFRESH_PERIOD;
	RefreshAction action;

	for (auto& campaignItem: campaigns) {

		CHECK(_campaignOver)
		
		CHECK(_fundingComplete)
		
		CHECK(_waitingOver)

    CHECK(_isRefunding)
    
    CHECK(_runExchange)
    
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
