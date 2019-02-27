#include "scrugex.hpp"

using namespace eosio;
using namespace std;

void scrugex::transfer(name from, name to, asset quantity, string memo) {
	if (to != _self) { return; }
	require_auth(from);

	// check transfer
	eosio_assert(quantity.symbol.is_valid(), "invalid quantity");
	eosio_assert(quantity.amount > 0, "only positive quantity allowed");
	eosio_assert(quantity.symbol == EOS_SYMBOL, "only EOS tokens allowed");
	
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
	auto isNotFounder = eosAccount != campaignItem->founderEosAccount;
	eosio_assert(isNotFounder, "campaign founder can not contribute");
	
  // get userId from kyc or eosAccount
  uint64_t userId = _verify(eosAccount, campaignItem->kycEnabled);
	
	eosio_assert(time > campaignItem->startTimestamp, "campaign has not started yet");
	eosio_assert(time < campaignItem->endTimestamp, "campaign has ended");
	eosio_assert(campaignItem->status == Status::funding, "campaign is not running");

	// check if allowed to invest this amount
	asset previous = _getContributionQuantity(scope, userId);
	asset total = previous + quantity;
	
	asset max = get_percent(campaignItem->hardCap, campaignItem->maxUserContributionPercent);
	asset min = get_percent(campaignItem->hardCap, campaignItem->minUserContributionPercent);
	eosio_assert(max > total, "you can not contribute this much");
	eosio_assert(min < total, "you can not contribute this little");

	// to-do check campaign cap?

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
	
} // void scrugex::transfer


void scrugex::newcampaign(name founderEosAccount, asset softCap, asset hardCap, 
		uint64_t initialFundsReleasePercent, bool kycEnabled,
		uint64_t maxUserContributionPercent, uint64_t minUserContributionPercent,
		uint64_t startTimestamp, uint64_t endTimestamp, vector<milestoneInfo> milestones) {

	require_auth(founderEosAccount);

	// check for duplicate campaign
	campaigns_i campaigns(_self, _self.value);
	
	// to-do validate arguments (make sure it's complete)
	eosio_assert(softCap < hardCap, "hard cap should be higher than soft cap");
	eosio_assert(startTimestamp < endTimestamp, "campaign end can not be earlier than campaign start");
	eosio_assert(milestones.size() > 0, "no milestones passed");
	
	eosio_assert(initialFundsReleasePercent < 50,
		"initial funds release can not be higher than 50%");

	campaigns.emplace(_self, [&](auto& r) {
		r.campaignId = campaigns.available_primary_key();
		r.founderEosAccount = founderEosAccount;
		r.softCap = softCap;
		r.hardCap = hardCap;
		r.initialFundsReleasePercent = initialFundsReleasePercent;
		r.maxUserContributionPercent = maxUserContributionPercent;
		r.minUserContributionPercent = minUserContributionPercent;
		r.startTimestamp = startTimestamp;
		r.endTimestamp = endTimestamp;
		r.raised = asset(0, EOS_SYMBOL);
		r.currentMilestone = 0;
		r.status = Status::funding;
		r.kycEnabled = kycEnabled;
		r.releasedPercent = 0;
		r.waitingEndTimestamp = 0;
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

	// to-do maybe call refresh on this campaign to start a voting?

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
	});
	
	// to-do check if should maybe end the voting
	
} // void scrugex::vote


void scrugex::extend(uint64_t campaignId) {
	campaigns_i campaigns(_self, _self.value);
	auto campaignItem = campaigns.find(campaignId);
	eosio_assert(campaignItem != campaigns.end(), "campaign does not exist");

	require_auth(campaignItem->founderEosAccount);
	
	_startvote(campaignId, VoteKind::extendDeadline);
	
} // void scrugex::extend

// to-do refactor
void scrugex::refresh() {
	require_auth(_self);

	auto now = time_ms();
	campaigns_i campaigns(_self, _self.value);
	
	// refresh again in 5 minutes if nothing else turns up
	uint64_t nextRefreshTime = 300;

	for (auto& campaignItem: campaigns) {

		if (now < campaignItem.endTimestamp || 			// still gathering money
			campaignItem.status == Status::closed) {	// over
			
			continue;
		}
		
		// check if waiting time has passed 
		if (campaignItem.status == Status::waiting && campaignItem.waitingEndTimestamp < now) {
		  
	    // founder failed to act, refunding
		  _refund(campaignItem.campaignId);
		  
      // don't schedule refresh if a campaign is refunding
      nextRefreshTime = 0;
			break;
		}

    // check if still funding but did not reach soft cap
		if (campaignItem.status == Status::funding && campaignItem.raised < campaignItem.softCap) {

			// did not reach soft cap, refund all money
		  _refund(campaignItem.campaignId);
		  
      // don't schedule refresh if a campaign is refunding
      nextRefreshTime = 0;
			break;
		}
		
		// check if supposed to be refunding
		if (campaignItem.status == Status::refunding || campaignItem.status == Status::distributing) {
		  
		  // schedule deferred transaction to refund or ditribute
		  // this should fail if another one is scheduled
		  _pay(campaignItem.campaignId);
		  
		  // don't schedule refresh if a campaign is refunding
		  nextRefreshTime = 0;
		  break;
		}

    // initialize milestones mode if funding complete
		if (campaignItem.status == Status::funding) {

			// release initial funds
			auto percent = get_percent(campaignItem.raised.amount,
									   campaignItem.initialFundsReleasePercent);

			auto quantity = campaignItem.raised;
			quantity.amount = percent;
			_transfer(campaignItem.founderEosAccount, quantity, "ScrugeX: Initial Funds");

			campaigns.modify(campaignItem, same_payer, [&](auto& r) {
			  r.releasedPercent += campaignItem.initialFundsReleasePercent;
				r.status = Status::milestone;
			});

      // complete this transaction and run next one in a second
      nextRefreshTime = 1;
			break;
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
			if (votingItem.milestoneId == milestoneId &&
				votingItem.active &&
				votingItem.kind == VoteKind::extendDeadline) {

				// to-do check if can close vote earlier

        // extend voting should be over  
				if (votingItem.endTimestamp < now || votingItem.voters == campaignItem.backersCount) {
				  
				  // stop it
					_stopvote(campaignItem.campaignId);

          // calculate decision
					if (votingItem.positiveVotes >= get_percent(votingItem.voters, T1)) {

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
					
				  // complete this transaction and run next one in a second
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
				if (votingItem.milestoneId == milestoneId &&
					votingItem.active &&
					votingItem.kind == VoteKind::milestoneResult) {

          // vote has just ended, don't start another one
					shouldStartNextMilestoneVoting = false;
					
					// to-do check if can close vote earlier
					
					// milestone voting should be over  
					if (votingItem.endTimestamp < now || votingItem.voters == campaignItem.backersCount) {
					  
					  // stop it
						_stopvote(campaignItem.campaignId);
						
						// calculate decision
						if (votingItem.positiveVotes >= get_percent(votingItem.voters, T1)) {
						  
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
								// campaign ended successfully
								campaigns.modify(campaignItem, same_payer, [&](auto& r) {
									r.status = Status::closed;
								});
							}
							else {
							  
							  // switch milestone
								campaigns.modify(campaignItem, same_payer, [&](auto& r) {
									r.currentMilestone = nextMilestoneId;
									r.status = Status::milestone;
								});
							}
						}
						else {
						  
							// milestone vote failed
							// wait for some time for founder to return funds or start extend voting
							campaigns.modify(campaignItem, same_payer, [&](auto& r) {
								r.status = Status::waiting;
								r.waitingEndTimestamp = now + DAY * 7; // to-do USE ACTUAL VALUE
							});
							
							// to-do actual waiting
						}
						
						// complete this transaction and run next one in a second
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


void scrugex::send(name eosAccount, uint64_t campaignId) {
  require_auth(_self);
  
  // fetch campaign
	campaigns_i campaigns(_self, _self.value);
	auto campaignItem = campaigns.find(campaignId);
	eosio_assert(campaignItem != campaigns.end(), "campaign does not exist");
	auto scope = campaignItem->campaignId;
	
	// this action only works when campaign is automatically refunding
	// to request failed refund after campaign was closed, there will be another action
	eosio_assert(campaignItem->status == Status::refunding || campaignItem->status == Status::distributing, 
	  "this campaign is not paying anyone right now");
  
  // get first in contributions
  contributions_i contributions(_self, scope);
	auto contributionItem = contributions.find(eosAccount.value);
	
	eosio_assert(contributionItem->isPaid == false, "this user has already been paid");

  if (campaignItem->status == Status::refunding) {
    
    uint64_t refundPercent = 100 - campaignItem->releasedPercent;
    
    uint64_t amount = get_percent(contributionItem->quantity.amount, refundPercent);
    _transfer(eosAccount, asset(amount, EOS_SYMBOL), "ScrugeX: Refund for campaign");
  }  
  
  // remove from contributions
  contributions.modify(contributionItem, same_payer, [&](auto& r) {
     r.attemptedPayment = true;
     r.isPaid = true;
  });

} // void scrugex::send


void scrugex::pay(uint64_t campaignId) {
  require_auth(_self);
  
  // fetch campaign
	campaigns_i campaigns(_self, _self.value);
	auto campaignItem = campaigns.find(campaignId);
	eosio_assert(campaignItem != campaigns.end(), "campaign does not exist");
	auto scope = campaignItem->campaignId;
	
	eosio_assert(campaignItem->status == Status::refunding || campaignItem->status == Status::distributing, 
	    "this campaign is not paying anyone right now");
  
  // get first in contributions
  contributions_i contributions(_self, scope);
  auto notAttemptedContributions = contributions.get_index<"byap"_n>();
	auto item = notAttemptedContributions.find(0);
	
  // if doesn't exist, close campaign, go back to refresh cycle
  if (item == notAttemptedContributions.end()) {
      campaigns.modify(campaignItem, same_payer, [&](auto& r) {
				r.status = Status::closed;
			});
			_scheduleRefresh(1);
			return;
  }
  
	// set attempted payment
	auto eosAccount = item->eosAccount;
	auto contributionItem = contributions.find(eosAccount.value);
  contributions.modify(contributionItem, same_payer, [&](auto& r) {
    r.attemptedPayment = true;
  });
  
  // schedule
  _scheduleNextPayout(eosAccount, campaignId);
  
} // void scrugex::startrefund


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
	}
	
} // void scrugex::destroy


// dispatch

extern "C" {
  
  void apply(uint64_t receiver, uint64_t code, uint64_t action) {
    
    if (code == receiver) {
  	  switch (action) {
        EOSIO_DISPATCH_HELPER(scrugex,
            (newcampaign)(vote)(extend)(refresh)(send)(pay) (destroy))
  	  }
    }
  	else if (code == "eosio.token"_n.value && action == "transfer"_n.value) {
  	  execute_action(name(receiver), name(code), &scrugex::transfer);
  	}
  }
};

