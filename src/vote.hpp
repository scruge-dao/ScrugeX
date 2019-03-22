// Copyright © Scruge 2019.
// This file is part of ScrugeX.
// Created by Yaroslav Erohin.

void scrugex::vote(name eosAccount, uint64_t campaignId, bool vote) {
  _assertPaused();
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

  auto now = time_ms();
	eosio_assert(votingItem != voting.end(), "voting is not currently held");
  eosio_assert(votingItem->endTimestamp > now, "voting is not currently held");

	voters_i voters(_self, scope);
	auto item = voters.find(userId);
	eosio_assert(item == voters.end(), "you have already voted");
	
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
	
	if (votingItem->voters == campaignItem->backersCount) {
	  _scheduleRefresh(0);
	}
	
} // void scrugex::vote
