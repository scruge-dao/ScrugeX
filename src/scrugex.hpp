#pragma once
#include <eosiolib/print.hpp>
#include <eosiolib/transaction.hpp>
#include <eosiolib/asset.hpp>

#include "constants.hpp"
#include "methods.hpp"

using namespace eosio;
using namespace std;

CONTRACT scrugex : public contract {

public:
	using contract::contract;
	
	scrugex(name receiver, name code, datastream<const char*> ds)
		: contract(receiver, code, ds) {}

	struct milestoneInfo { uint64_t deadline, fundsReleasePercent; };

	void transfer(name from, name to, asset quantity, string memo);

	ACTION newcampaign(
		uint64_t campaignId, uint64_t founderUserId, name founderEosAccount,
		asset softCap, asset hardCap, uint64_t initialFundsReleasePercent,
		asset maxUserContribution, asset minUserContribution,
		uint64_t publicTokenPercent, uint64_t tokenSupply,
		uint64_t startTimestamp, uint64_t endTimestamp,
		uint64_t annualInflationPercentStart, uint64_t annualInflationPercentEnd,
		vector<milestoneInfo> milestones);

	ACTION vote(name eosAccount, uint64_t userId, uint64_t campaignId, bool vote);

	ACTION extend(uint64_t campaignId);

	ACTION refresh();

	ACTION destroy();

	ACTION send(name eosAccount);

	ACTION pay(uint64_t campaignId);

private:

	enum Status: uint8_t { funding = 0, milestone = 1, activeVote = 2, waiting = 3, closed = 4 };

	void _transfer(name account, asset quantity, string memo, name contract) {
		action(
			permission_level{ _self, "active"_n },
			contract, "transfer"_n,
			make_tuple(_self, account, quantity, memo)
		).send();
	}

	void _transfer(name account, asset quantity, string memo) {
		_transfer(account, quantity, memo, "eosio.token"_n);
	}

	void _stopvote(uint64_t campaignId) {
		campaigns_i campaigns(_self, 0);
		auto campaignItem = campaigns.find(campaignId);
		eosio_assert(campaignItem != campaigns.end(), "campaign does not exist");

		auto scope = campaignItem->scope;

		milestones_i milestones(_self, scope);
		auto milestoneId = campaignItem->currentMilestone;

		// create a voting record
		voting_i voting(_self, scope);
		
		auto votingItem = voting.begin();
		while (votingItem != voting.end()) {
			if (votingItem->active) { break; }
			votingItem++;
		}
		eosio_assert(votingItem != voting.end(), "voting is not currently held");

		voting.modify(votingItem, _self, [&](auto& r) {
			r.active = false;
		});
	}

	void _startvote(uint64_t campaignId, uint8_t kind) {
		campaigns_i campaigns(_self, 0);
		auto campaignItem = campaigns.find(campaignId);
		eosio_assert(campaignItem != campaigns.end(), "campaign does not exist");

		auto scope = campaignItem->scope;

		// check if campaign ended & successfully
		// to-do

		// get current milestone
		milestones_i milestones(_self, scope);
		auto milestoneId = campaignItem->currentMilestone;

		// create a voting record
		voting_i voting(_self, scope);

		// check if this/other vote exists for this milestone
		auto voteId = milestoneId * 100 + kind;
		auto thisVote = voting.find(voteId);

		eosio_assert(thisVote == voting.end(), "this voting already exists");
		
		// to-do set correct time
		// check other votes
		auto now = time_ms();
		auto start = now;//  + DAY * 14; // 14 days notification 
		auto end = start + DAY * 14; // 3 days vote time

		// delete voters from previous voting
		voters_i voters(_self, scope);
		auto v_item = voters.begin();
		while(v_item != voters.end()) { v_item = voters.erase(v_item); }

		voting.emplace(_self, [&](auto& r) {
			r.voteId = voteId;
			r.kind = kind;
			r.milestoneId = milestoneId;
			r.startTimestamp = start;
			r.endTimestamp = end;
			r.voters = 0;
			r.positiveVotes = 0;
			r.active = true;
		});

		campaigns.modify(campaignItem, _self, [&](auto& r) {
			r.status = Status::activeVote;
		});
	} // void _startvote

	void _updateCampaignsCount(uint64_t scope) {
		information_i information(_self, 0);
		auto lambda = [&](auto& row) {
			row.key = 0;
			row.campaignsCount = scope + 1;
		};

		if (information.begin() == information.end()) {
			information.emplace(_self, lambda);
		}
		else { information.modify(information.begin(), _self, lambda); }
		
	} // void _updateCampaignsCount

	uint64_t _getCampaignsCount() {
		information_i information(_self, 0);
		for (auto& item : information) {
			return item.campaignsCount;
		}
		return 0;
	} // uint64_t _getCampaignsCount

	asset _getContributionQuantity(int64_t scope, uint64_t userId) {
		asset total = asset(0, EOS_SYMBOL);
		eosio_assert(scope >= 0, "campaign does not exist");

		contributions_i contributions(_self, scope);
		for (auto& item : contributions) {
        	if (item.userId == userId) {
        	   total += item.quantity;
        	}
    	}
    	return total;
	} // _getContributionQuantity

	int64_t _getScope(uint64_t campaignId) {
		campaigns_i campaigns(_self, 0);
		auto campaignItem = campaigns.find(campaignId);
		if (campaignItem != campaigns.end()) {
			return campaignItem->scope;
		}
		return -1;
	} // int64_t _getScope

  // structs

	TABLE information {
		uint64_t key;
		uint64_t campaignsCount;

		uint64_t primary_key() const { return key; }
	};

	TABLE campaigns {
		uint8_t status;
		uint64_t campaignId;
		uint64_t founderUserId;
		name founderEosAccount;
		uint64_t startTimestamp;
		uint64_t endTimestamp;

		asset softCap;
		asset hardCap;
		uint64_t initialFundsReleasePercent;
		asset maxUserContribution;
		asset minUserContribution;
		uint64_t publicTokenPercent;
		uint64_t tokenSupply;

		uint64_t scope;
		asset raised;
		uint64_t backersCount;
		uint8_t currentMilestone;

		uint64_t annualInflationPercentStart;
		uint64_t annualInflationPercentEnd;

		uint64_t primary_key() const { return campaignId; }
	};

	TABLE milestones {
		uint8_t id;
		uint64_t deadline;
		uint64_t fundsReleasePercent;

		uint64_t primary_key() const { return id; }
	};

	TABLE contribution {
		uint64_t userId;
		name eosAccount;
		asset quantity;
		
		// refund/token distribution flags
		bool attemptedPayment;  // did attemt payment
		bool isPaid;            // payment was successful
 
		uint64_t primary_key() const { return eosAccount.value; }
		uint64_t by_userId() const { return userId; }
	};

	TABLE voting {
		uint64_t voteId;
		uint8_t kind;			// 0 - extend, 1 - deadline
		uint8_t milestoneId;
		int64_t voters;
		int64_t positiveVotes;
		uint64_t endTimestamp;
		uint64_t startTimestamp;
		bool active;

		uint64_t primary_key() const { return voteId; }
	};

	struct [[eosio::table]] voters {
		uint64_t userId;
		uint8_t voteId;
		bool vote;

		uint64_t primary_key() const { return userId; }
	};

  // tables

	typedef multi_index<"information"_n, information> information_i;
	typedef multi_index<"campaigns"_n, campaigns> campaigns_i;
	typedef multi_index<"milestones"_n, milestones> milestones_i;
	typedef multi_index<"voters"_n, voters> voters_i;
	typedef multi_index<"voting"_n, voting> voting_i;

	typedef multi_index<"contribution"_n, contribution,
			indexed_by<"byuserid"_n, const_mem_fun<contribution, uint64_t,
												   &contribution::by_userId>>> contributions_i;
	
	// to access kyc/aml table
	
	struct account {
		uint64_t userId;
		name eosAccount;

		uint64_t primary_key() const { return userId; }
		uint64_t by_eos() const { return eosAccount.value; }
	};

	typedef multi_index<"accounts"_n, account,
		indexed_by<"byeos"_n, const_mem_fun<account, uint64_t,
											&account::by_eos>>> accounts_i;
};