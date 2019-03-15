void scrugex::newcampaign(name founderEosAccount, asset softCap, asset hardCap, 
		asset supplyForSale, name tokenContract, uint64_t initialFundsReleasePercent, bool kycEnabled,
		uint64_t maxUserContributionPercent, asset minUserContribution,
		uint64_t startTimestamp, uint64_t campaignDuration, vector<milestoneInfo> milestones) {

  _assertPaused();
	require_auth(founderEosAccount);
	
	auto investmentSymbol = hardCap.symbol;
	auto now = time_ms();
	
	eosio_assert(!kycEnabled, "kyc is not implemented yet");
	
  eosio_assert(tokenContract != "eosio.token"_n && supplyForSale.symbol != EOS_SYMBOL, 
    "you can not raise money for EOS");
	
	eosio_assert(softCap < hardCap, "hard cap should be higher than soft cap");
	eosio_assert(milestones.size() > 0, "no milestones passed");
	
	eosio_assert(maxUserContributionPercent > 0, "max user contribution percent can not be 0%");
	eosio_assert(maxUserContributionPercent < 100, "max user contribution percent can not be above 100%");
	
	eosio_assert(campaignDuration >= MIN_CAMPAIGN_DURATION, "campaign should be longer than 2 weeks");
	eosio_assert(campaignDuration <= MAX_CAMPAIGN_DURATION, "campaign should be shorter than 8 weeks");
	
	eosio_assert(minUserContribution.symbol == softCap.symbol, "min contribution symbols mismatch");
	eosio_assert(minUserContribution < get_percent(hardCap, maxUserContributionPercent),
	   "min contribution should not be lower than max");
	
	eosio_assert(startTimestamp > now, "campaign can not start in the past");
	
	eosio_assert(supplyForSale.symbol.is_valid(), "invalid supply for sale");
	eosio_assert(hardCap.symbol.is_valid(), "invalid hard cap");
	eosio_assert(softCap.symbol.is_valid(), "invalid soft cap");
	
	eosio_assert(hardCap.symbol == softCap.symbol, "cap symbols mismatch");
	eosio_assert(hardCap.symbol == EOS_SYMBOL, "only EOS can be used to receive investments");
	
	eosio_assert(initialFundsReleasePercent <= 25,
		"initial funds release can not be higher than 25%");

	campaigns_i campaigns(_self, _self.value);
  auto campaignId = campaigns.available_primary_key();

	campaigns.emplace(founderEosAccount, [&](auto& r) {
		r.campaignId = campaignId;
		r.founderEosAccount = founderEosAccount;
		r.softCap = softCap;
		r.hardCap = hardCap;
		r.initialFundsReleasePercent = initialFundsReleasePercent;
		r.maxUserContributionPercent = maxUserContributionPercent;
		r.minUserContribution = minUserContribution;
		r.startTimestamp = startTimestamp;
		r.endTimestamp = startTimestamp + campaignDuration;
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
	});
	
	// update campaigns count
	information_i information(_self, _self.value);
	auto infoItem = information.begin();
	uint64_t scope = 0;
	
	if (information.begin() == information.end()) {
	  information.emplace(_self, [&](auto& r) {
	    r.campaignsCount = 1;
	    r.isPaused = false;
	    r.ramFund = asset(0, EOS_SYMBOL);
	  });
	}
	else {
	  scope = infoItem->campaignsCount;
	  information.modify(information.begin(), same_payer, [&](auto& r) { 
	    r.campaignsCount = scope + 1; 
	  });
	}

  // save milestones
	milestones_i table(_self, scope);
	auto totalFundsRelease = initialFundsReleasePercent;

	for (auto milestone: milestones) {

		eosio_assert(milestone.duration >= MIN_MILESTONE_DURATION,
		  "milestone duration should be longer");

		eosio_assert(milestone.duration <= MAX_MILESTONE_DURATION,
		  "milestone duration should be shorter");

		eosio_assert(milestone.fundsReleasePercent <= 25,
		  "milestone funds release can not be higher than 25%");

		totalFundsRelease += milestone.fundsReleasePercent;

		eosio_assert(totalFundsRelease <= 100,
			"total funds release can not go over 100%");

		table.emplace(founderEosAccount, [&](auto& r) {
			r.id = table.available_primary_key();
			r.duration = milestone.duration;
			r.fundsReleasePercent = milestone.fundsReleasePercent;
		});
	}

	eosio_assert(totalFundsRelease == 100, 
		"total funds release can be less than 100%");
		
} // void scrugex::newcampaign