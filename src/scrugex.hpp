#include <cmath>
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

	struct milestoneInfo { uint64_t duration, fundsReleasePercent; };

	void transfer(name from, name to, asset quantity, string memo);

	ACTION newcampaign(name founderEosAccount, asset softCap, asset hardCap, 
	  asset supplyForSale, name tokenContract, uint64_t initialFundsReleasePercent, bool kycEnabled,
		uint64_t maxUserContributionPercent, uint64_t minUserContributionPercent,
		uint64_t startTimestamp, uint64_t campaignDuration, vector<milestoneInfo> milestones);

	ACTION vote(name eosAccount, uint64_t campaignId, bool vote);
	ACTION extend(uint64_t campaignId);
	ACTION refund(uint64_t campaignId);
	ACTION refresh();
	ACTION zdestroy();
	ACTION send(name eosAccount, uint64_t campaignId);
	ACTION take(name eosAccount, uint64_t campaignId);
	ACTION pay(uint64_t campaignId);
	ACTION pause(bool value);
	ACTION buyram();

private:

	enum Status: uint8_t { funding = 0, milestone = 1, activeVote = 2, waiting = 3,
							 refunding = 4, distributing = 5 };
	
	enum VoteKind: uint8_t { extendDeadline = 0, milestoneResult = 1 };
	
	// structs

	TABLE information {
		uint64_t campaignsCount;
		bool isPaused;
		asset ramFund;
  
		uint64_t primary_key() const { return 0; }
	};

	TABLE campaigns {
		uint8_t status;   // Status
		uint64_t campaignId;
		name founderEosAccount;
		uint64_t startTimestamp;
		uint64_t endTimestamp;
		uint64_t waitingEndTimestamp;

    // new tokens
    name tokenContract;
		asset supplyForSale; // amount of tokens to sale
		bool tokensReceived;
		
		// investment tokens (EOS)
		asset softCap;
		asset hardCap;
		asset raised;
		asset excessReturned;
		
		uint64_t initialFundsReleasePercent;
		uint64_t maxUserContributionPercent;
		uint64_t minUserContributionPercent;
		uint64_t releasedPercent;
		
		uint64_t backersCount;
		uint8_t currentMilestone;
		bool kycEnabled;
		
		uint64_t primary_key() const { return campaignId; }
	};

	TABLE milestones {
		uint8_t id;
		uint64_t duration;
		uint64_t fundsReleasePercent;
		uint64_t startTimestamp;

		uint64_t primary_key() const { return id; }
	};

	TABLE contribution {
		uint64_t userId;
		name eosAccount;
		asset quantity;
		
		// distribution flags
		bool attemptedPayment;  // did attemt payment
		bool isPaid;            // payment was successful
 
		uint64_t primary_key() const { return eosAccount.value; }
		uint64_t by_amount_desc() const { return UINT64_MAX - quantity.amount; }
		uint64_t by_userId() const { return userId; }
		uint64_t by_ap() const { return attemptedPayment ? 1 : 0; }
	};
	
	TABLE excessfunds {
  	asset quantity;
  	name eosAccount;
	
		// distribution flags
		bool attemptedPayment;  // did attemt payment
		bool isPaid;            // payment was successful
 
	  uint64_t primary_key() const { return eosAccount.value; }
		uint64_t by_eos_account() const { return eosAccount.value; }
		uint64_t by_ap() const { return attemptedPayment ? 1 : 0; }
	};

	TABLE voting {
		uint64_t voteId;
		uint8_t kind;   // VoteKind
		uint8_t milestoneId;
		uint64_t voters;
		uint64_t positiveVotes;
		
		uint64_t votedWeight;
		uint64_t positiveWeight;
		
		uint64_t endTimestamp;
		uint64_t startTimestamp;
		bool active;

		uint64_t primary_key() const { return voteId; }
    uint64_t by_active() const { return active ? 0 : 1; }
	};

	TABLE voters {
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
	
	typedef multi_index<"voting"_n, voting,
	  indexed_by<"byactive"_n, const_mem_fun<voting, uint64_t, &voting::by_active>>
	    > voting_i;
	
	typedef multi_index<"excessfunds"_n, excessfunds,
		indexed_by<"byeosaccount"_n, const_mem_fun<excessfunds, uint64_t, &excessfunds::by_eos_account>>,
		indexed_by<"byap"_n, const_mem_fun<excessfunds, uint64_t, &excessfunds::by_ap>>
			> excessfunds_i;

	typedef multi_index<"contribution"_n, contribution,
		indexed_by<"byuserid"_n, const_mem_fun<contribution, uint64_t, &contribution::by_userId>>,
		indexed_by<"byap"_n, const_mem_fun<contribution, uint64_t, &contribution::by_ap>>,
		indexed_by<"byamountdesc"_n, const_mem_fun<contribution, uint64_t, &contribution::by_amount_desc>>
			> contributions_i;
	
	// to access kyc/aml table
	
	struct account {
		uint64_t id;
		name eosAccount;

		uint64_t primary_key() const { return eosAccount.value; }
		uint64_t identifier() const { return id; }
	};

	typedef multi_index<"accounts"_n, account,
		indexed_by<"identifier"_n, const_mem_fun<account, uint64_t,
      &account::identifier>>> accounts_i;
      
  // to request ram price
  
  struct exchange_state {
    asset supply;

    struct connector {
       asset balance;
       double weight = .5;
    };

    connector base;
    connector quote;

    uint64_t primary_key()const { return supply.symbol.raw(); }
  };

   typedef eosio::multi_index< "rammarket"_n, exchange_state > rammarket_i;
  
  
	// helper methods
	void _pay(uint64_t campaignId);
	void _transfer(name account, asset quantity, string memo, name contract);
	void _transfer(name account, asset quantity, string memo);
	void _send(name eosAccount, uint64_t campaignId);
	void _scheduleRefresh(uint64_t nextRefreshTime);
	void _schedulePay(uint64_t campaignId);
	void _scheduleSend(name eosAccount, uint64_t campaignId);
  bool _willRefundExcessiveFunds(uint64_t campaignId);
	uint64_t _verify(name eosAccount, bool kycEnabled);
	void _stopvote(uint64_t campaignId);
	void _startvote(uint64_t campaignId, uint8_t kind);
	void _refund(uint64_t campaignId);
	asset _getContributionQuantity(uint64_t scope, uint64_t userId);
	void _assertPaused();
	asset _getRamPriceKB();

  // refresh cycle methods
  enum RefreshAction: uint8_t { doneT = 0, done = 1, skip = 2, pass = 3 };
  typedef tuple<uint64_t, RefreshAction> param;
  param _campaignFunding(const campaigns& campaignItem, campaigns_i& campaigns);
  param _initialRelease(const campaigns& campaignItem, campaigns_i& campaigns);
  param _waiting(const campaigns& campaignItem, campaigns_i& campaigns);
  param _refunding(const campaigns& campaignItem, campaigns_i& campaigns);
  param _voting(const campaigns& campaignItem, campaigns_i& campaigns);
  param _extendDeadlineVoting(const voting& votingItem, const campaigns& campaignItem, campaigns_i& campaigns);
  param _milestoneVoting(const voting& votingItem, const campaigns& campaignItem, campaigns_i& campaigns);
  
}; // CONTRACT scrugex