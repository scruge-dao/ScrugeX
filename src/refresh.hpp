#define PASS return make_tuple(0, RefreshAction::pass);
#define SKIP return make_tuple(0, RefreshAction::skip);
#define DONE_ return make_tuple(0, RefreshAction::done);
#define DONE(x) return make_tuple((x), RefreshAction::doneT);

#define INIT uint64_t t = 0, nextRefreshTime = REFRESH_PERIOD; RefreshAction action;
#define R_CHECK(x) tie(t, action) = (x)(campaignItem, campaigns); if (action == RefreshAction::pass) {} else if (action == RefreshAction::doneT) { nextRefreshTime = t; break; } else if (action == RefreshAction::done) { break; } else if (action == RefreshAction::skip) { continue; }
#define V_CHECK(x) tie(t, action) = (x)(*votingItem, campaignItem, campaigns); if (action == RefreshAction::pass) {} else if (action == RefreshAction::doneT) { DONE(t) } else if (action == RefreshAction::done) { DONE_ } else if (action == RefreshAction::skip) { SKIP }
#define X_CHECK(x) tie(t, action) = (x)(campaignItem, campaigns); if (action == RefreshAction::pass) {} else if (action == RefreshAction::doneT) { DONE(t) } else if (action == RefreshAction::done) { DONE_ } else if (action == RefreshAction::skip) { SKIP }
#define param scrugex::param scrugex::

param _campaignFunding(const campaigns& campaignItem, campaigns_i& campaigns) {
  if (time_ms() < campaignItem.endTimestamp) {
		SKIP
	}
	if (campaignItem.status == Status::refunding || campaignItem.status == Status::distributing) {
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

    milestones_i milestones(_self, campaignItem.campaignId);
    auto firstMilestone = milestones.find(0);
    milestones.modify(firstMilestone, same_payer, [&](auto& r) {
			r.startTimestamp = time_ms();
    });

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
			campaignItem.status == Status::distributing) {
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
  
  V_CHECK(_extendDeadlineVoting)
  
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
	if (votingItem.positiveWeight >= get_percent(votingItem.votedWeight, EXTEND_VOTING_THRESHOLD)) {

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
	if (votingItem.positiveWeight < get_percent(votingItem.votedWeight, MILESTONE_VOTING_THRESHOLD)) {
	  
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
	}
	
  DONE(1)
	
} // PARAM _milestoneVoting

void scrugex::refresh() {
  _assertPaused();
	require_auth(_self);
	
	INIT
	
	campaigns_i campaigns(_self, _self.value);
	for (auto& campaignItem: campaigns) {

		R_CHECK(_campaignFunding)
		
		R_CHECK(_initialRelease)
		
		R_CHECK(_waiting)

    R_CHECK(_refunding)
    
		R_CHECK(_voting)
	}

	if (nextRefreshTime != 0) {
		_scheduleRefresh(nextRefreshTime);
	}
	
} // void refresh
