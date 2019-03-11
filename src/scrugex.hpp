#include <cmath>
#include <eosiolib/transaction.hpp>
#include <eosiolib/asset.hpp>

#include "constants.hpp"
#include "methods.hpp"

using namespace eosio;
using namespace std;

#define PRINT(x, y) eosio::print(x); eosio::print(y); eosio::print("\n");
#define PRINT_(x) eosio::print(x); eosio::print("\n");

CONTRACT scrugex : public contract {

public:
	using contract::contract;
	
	scrugex(name receiver, name code, datastream<const char*> ds)
		: contract(receiver, code, ds) {}

	struct milestoneInfo { uint64_t deadline, fundsReleasePercent; };

	void transfer(name from, name to, asset quantity, string memo);

	ACTION newcampaign(name founderEosAccount, asset softCap, asset hardCap, 
	asset supplyForSale, name tokenContract, uint64_t initialFundsReleasePercent, bool kycEnabled,
		uint64_t maxUserContributionPercent, uint64_t minUserContributionPercent,
		uint64_t startTimestamp, uint64_t endTimestamp, vector<milestoneInfo> milestones);

	ACTION vote(name eosAccount, uint64_t campaignId, bool vote);

	ACTION extend(uint64_t campaignId);
	
	ACTION refund(uint64_t campaignId);

	ACTION refresh();

	ACTION destroy();

	ACTION send(name eosAccount, uint64_t campaignId);
	
	ACTION take(name eosAccount, uint64_t campaignId);

	ACTION pay(uint64_t campaignId);
	
	// exchange
	
	ACTION buy(name eosAccount, uint64_t campaignId, asset quantity, asset sum);
	
	ACTION sell(name eosAccount, uint64_t campaignId, asset quantity);

private:

	enum Status: uint8_t { funding = 0, milestone = 1, activeVote = 2, waiting = 3,
							 refunding = 4, distributing = 5, excessReturning = 6 };
	
	enum ExchangeStatus: uint8_t { inactive = 0, selling = 1, buying = 2 };
	
	enum VoteKind: uint8_t { extendDeadline = 0, milestoneResult = 1 };
	
	void _pay(uint64_t campaignId) {
		action(
			permission_level{ _self, "active"_n },
			_self, "pay"_n,
			make_tuple(campaignId)
		).send();
	} // void _pay

	void _transfer(name account, asset quantity, string memo, name contract) {
		action(
			permission_level{ _self, "active"_n },
			contract, "transfer"_n,
			make_tuple(_self, account, quantity, memo)
		).send();
	} // void _transfer

	void _transfer(name account, asset quantity, string memo) {
		_transfer(account, quantity, memo, "eosio.token"_n);
	} // void _transfer
	
	void _send(name eosAccount, uint64_t campaignId) {
	  action(
			permission_level{ _self, "active"_n },
			_self, "send"_n,
			make_tuple(eosAccount, campaignId)
		).send();
	} // void _send
	
	void _scheduleRefresh(uint64_t nextRefreshTime) {
  	cancel_deferred("refresh"_n.value);
  	
  	transaction t{};
  	t.actions.emplace_back(permission_level(_self, "active"_n),
  									 _self, "refresh"_n,
  									 make_tuple());
  	t.delay_sec = nextRefreshTime;
  	t.send("refresh"_n.value, _self, false);
  	
	} // void _scheduleRefresh
	
	void _schedulePay(uint64_t campaignId) {
		transaction t{};
		t.actions.emplace_back(permission_level(_self, "active"_n),
										 _self, "pay"_n,
										 make_tuple( campaignId ));
		t.delay_sec = 2;
		t.send(time_ms() + 1, _self, false);

	} //void _schedulePay
	
	void _scheduleSend(name eosAccount, uint64_t campaignId) {
  	auto now = time_ms();
  	transaction t{};
  	t.actions.emplace_back(permission_level(_self, "active"_n),
  									 _self, "send"_n,
  									 make_tuple( eosAccount, campaignId ));
  	t.delay_sec = 1;
  	t.send(time_ms(), _self, false);
	
	} // void _scheduleSend
	
  bool _willRefundExcessiveFunds(uint64_t campaignId) {
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

	uint64_t _verify(name eosAccount, bool kycEnabled) {
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
	
	void _stopvote(uint64_t campaignId) {
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

	void _startvote(uint64_t campaignId, uint8_t kind) {
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

	void _refund(uint64_t campaignId) {
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

	void _updateCampaignsCount(uint64_t scope) {
		information_i information(_self, _self.value);
		
		auto lambda = [&](auto& row) {
			row.campaignsCount = scope + 1;
		};

		if (information.begin() == information.end()) {
			information.emplace(_self, lambda);
		}
		else { information.modify(information.begin(), same_payer, lambda); }
		
	} // void _updateCampaignsCount

	uint64_t _getCampaignsCount() {
		information_i information(_self, _self.value);
		for (auto& item : information) {
			return item.campaignsCount;
		}
		return 0;
	} // uint64_t _getCampaignsCount

  // to-do optimize
	asset _getContributionQuantity(uint64_t scope, uint64_t userId) {
		contributions_i contributions(_self, scope);
		asset total = asset(0, EOS_SYMBOL); // use investment symbol
		for (auto& item : contributions) {
			if (item.userId == userId) {
				 total += item.quantity;
			}
		}
		return total;
	} // _getContributionQuantity

	// structs

	TABLE information {
		uint64_t campaignsCount;

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
		bool active;
		
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
		
		// distribution flags
		bool attemptedPayment;  // did attemt payment
		bool isPaid;            // payment was successful
 
		uint64_t primary_key() const { return eosAccount.value; }
		uint64_t by_userId() const { return userId; }
		uint64_t by_not_attempted_payment() const { return attemptedPayment ? 1 : 0; }
		uint64_t by_amount_desc() const { return numeric_limits<uint64_t>::max() - quantity.amount; }
	};
	
	TABLE excessfunds {
	asset quantity;
	name eosAccount;
	
		// distribution flags
		bool attemptedPayment;  // did attemt payment
		bool isPaid;            // payment was successful
 
	  uint64_t primary_key() const { return eosAccount.value; }
		uint64_t by_not_attempted_payment() const { return attemptedPayment ? 1 : 0; }
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
	};

	TABLE voters {
		uint64_t userId;
		uint8_t voteId;
		bool vote;

		uint64_t primary_key() const { return userId; }
	};
	
	TABLE exchangeinfo {
	  uint64_t milestoneId;
		uint8_t status;
		double previousPrice;
		double roundPrice;
		asset roundSellVolume;
		asset investorsFund;
		uint64_t priceTimestamp;
		uint64_t sellEndTimestamp;
		
		uint64_t primary_key() const { return 0; }
	};
	
  TABLE sellorders {
    uint64_t milestoneId;
    uint64_t key;
    name eosAccount;
    
    // to-do link with milestones to claim remaining eos from multiple exchange runs
    uint64_t userId;
    asset quantity;
    uint64_t timestamp;
    asset received;
    
		// distribution flags
		bool attemptedPayment;  // did attemt payment
		bool isPaid;            // payment was successful
 
    uint64_t primary_key() const { return key; }
		uint64_t by_not_attempted_payment() const { return attemptedPayment ? 1 : 0; }
  };

  TABLE buyorders {
    uint64_t milestoneId;
    uint64_t key;
    name eosAccount;
    // to-do link with milestones to claim remaining eos from multiple exchange runs 
    bool paymentReceived;
    uint64_t userId;
    asset quantity;
    asset sum;
    double price;
    uint64_t timestamp;
    asset purchased;
    double spent;
    
		// distribution flags
		bool attemptedPayment;  // did attemt payment
		bool isPaid;            // payment was successful
 
    // descending sort by key 
    uint64_t primary_key() const { return key; }
    
		uint64_t by_not_attempted_payment() const { return attemptedPayment ? 1 : 0; }
    
    // to-do smart sort by milestone -> price -> timestamp 
    uint64_t by_price() const { return numeric_limits<uint64_t>::max() - (uint64_t)(price * 1000000000.) ; } 
  };

	// tables

	typedef multi_index<"information"_n, information> information_i;
	typedef multi_index<"campaigns"_n, campaigns> campaigns_i;
	typedef multi_index<"milestones"_n, milestones> milestones_i;
	typedef multi_index<"voters"_n, voters> voters_i;
	typedef multi_index<"voting"_n, voting> voting_i;
	
	typedef multi_index<"excessfunds"_n, excessfunds,
		indexed_by<"byap"_n, const_mem_fun<excessfunds, uint64_t, &excessfunds::by_not_attempted_payment>>
			> excessfunds_i;

	typedef multi_index<"contribution"_n, contribution,
		indexed_by<"byuserid"_n, const_mem_fun<contribution, uint64_t, &contribution::by_userId>>,
		indexed_by<"byap"_n, const_mem_fun<contribution, uint64_t, &contribution::by_not_attempted_payment>>,
		indexed_by<"byamountdesc"_n, const_mem_fun<contribution, uint64_t, &contribution::by_amount_desc>>
			> contributions_i;
	
	// exchange
	
  typedef multi_index<"exchangeinfo"_n, exchangeinfo> exchangeinfo_i;
  
  typedef multi_index<"sellorders"_n, sellorders,
		indexed_by<"byap"_n, const_mem_fun<sellorders, uint64_t, &sellorders::by_not_attempted_payment>>
		  > sellorders_i;
  
  typedef multi_index<"buyorders"_n, buyorders,
    indexed_by<"bypricedesc"_n, const_mem_fun<buyorders, uint64_t, &buyorders::by_price>>,
		indexed_by<"byap"_n, const_mem_fun<buyorders, uint64_t, &buyorders::by_not_attempted_payment>>
      > buyorders_i;

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
  
	
  
  // refresh cycle 
  
  enum RefreshAction: uint8_t { doneT = 0, done = 1, skip = 2, pass = 3 };
  typedef tuple<uint64_t, RefreshAction> PARAM;
  #define PASS return make_tuple(0, RefreshAction::pass);
  #define SKIP return make_tuple(0, RefreshAction::skip);
  #define DONE_ return make_tuple(0, RefreshAction::done);
  #define DONE(x) return make_tuple((x), RefreshAction::doneT);
  
  PARAM _campaignOver(const campaigns& campaignItem, campaigns_i& campaigns) {
    if (time_ms() < campaignItem.endTimestamp || campaignItem.active == false) {
			SKIP
		}
		PASS 
  } // PARAM _campaignOver
  
  PARAM _fundingComplete(const campaigns& campaignItem, campaigns_i& campaigns) {
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
  
  		// start milestones
  		campaigns.modify(campaignItem, same_payer, [&](auto& r) {
  			r.releasedPercent += campaignItem.initialFundsReleasePercent;
  			r.status = Status::milestone;
  		});
		
  		DONE(1)
	  }
		PASS
  } // PARAM _fundingComplete
  
  PARAM _waitingOver(const campaigns& campaignItem, campaigns_i& campaigns) {
		if (campaignItem.status == Status::waiting && campaignItem.waitingEndTimestamp < time_ms()) {
			_refund(campaignItem.campaignId);
			DONE(1)
		}
		PASS 
  } // PARAM _waitingOver
  
  PARAM _isRefunding(const campaigns& campaignItem, campaigns_i& campaigns) {
		if (campaignItem.status == Status::refunding || 
				campaignItem.status == Status::distributing ||
				campaignItem.status == Status::excessReturning) {
			_schedulePay(campaignItem.campaignId);
			SKIP
		}
		PASS
  } // PARAM _isRefunding
  
  PARAM _voting(const campaigns& campaignItem, campaigns_i& campaigns) {
    uint64_t nextRefreshTime = REFRESH_PERIOD;
    
		milestones_i milestones(_self, campaignItem.campaignId);
		auto milestoneId = campaignItem.currentMilestone;
		auto currentMilestoneItem = milestones.find(milestoneId);
		voting_i voting(_self, campaignItem.campaignId);

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
		
		PASS
  } // PARAM _voting
  
  #define _CHECK(x) tie(t, action) = (x)(campaignItem); if (action == RefreshAction::pass) {} else if (action == RefreshAction::doneT) { DONE(t) } else if (action == RefreshAction::done) { DONE_ } else if (action == RefreshAction::skip) { SKIP }

  PARAM _runExchange(const campaigns& campaignItem, campaigns_i& campaigns) {
    uint64_t t = 0, nextRefreshTime = REFRESH_PERIOD;
	  RefreshAction action;

    exchangeinfo_i exchangeinfo(_self, campaignItem.campaignId);
    auto exchangeItem = exchangeinfo.begin();
    
    if (exchangeItem->status != ExchangeStatus::inactive) {
      
      _CHECK(_closeSell)
      
      _CHECK(_canClose)
    }
    PASS
  } // PARAM _runExchange
  
  PARAM _closeSell(const campaigns& campaignItem) {
    exchangeinfo_i exchangeinfo(_self, campaignItem.campaignId);
    auto exchangeItem = exchangeinfo.begin();
    
    if (exchangeItem->status == ExchangeStatus::selling && exchangeItem->sellEndTimestamp < time_ms()) {
      
      PRINT_("exchange: sell phase ended")
      
      sellorders_i sellorders(_self, campaignItem.campaignId);
      
      auto newStatus = sellorders.begin() == sellorders.end() ? 
        ExchangeStatus::inactive : ExchangeStatus::buying;
        
      exchangeinfo.modify(exchangeItem, same_payer, [&](auto& r) {
        r.status = newStatus;
      });
      
      DONE_
    }
    PASS 
  } // PARAM _sellOver
  
  PARAM _canClose(const campaigns& campaignItem) {
    campaigns_i campaigns(_self, _self.value);
    exchangeinfo_i exchangeinfo(_self, campaignItem.campaignId);
    auto exchangeItem = exchangeinfo.begin();
    
    if (exchangeItem->status == ExchangeStatus::buying) {
      uint64_t now = time_ms();
      
      // lower auction price when needed
      if (exchangeItem->priceTimestamp + EXCHANGE_PRICE_PERIOD < now) {
        exchangeinfo.modify(exchangeItem, same_payer, [&](auto& r) {
          r.priceTimestamp = now;
          r.roundPrice /= 5; // to-do formula
        });
      }
  
      buyorders_i buyorders(_self, campaignItem.campaignId);
      auto ordersByPrice = buyorders.get_index<"bypricedesc"_n>(); // to-do smart sort milestone -> price -> time
      
      auto sellVolume = exchangeItem->roundSellVolume.amount;
      double roundPrice = (double)exchangeItem->roundPrice;
      double pICO = (double)campaignItem.raised.amount / (double)campaignItem.supplyForSale.amount;
      
      vector<uint64_t> ids;
      
      for (auto& orderItem : ordersByPrice) {
        
        // process current exchange only
        if (orderItem.milestoneId != exchangeItem->milestoneId) { continue; }
        
        // skip unpaid orders
        if (!orderItem.paymentReceived) { continue; }
        
        if (orderItem.price < exchangeItem->roundPrice) { break; }
        
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
        
        PRINT_("exchange: exchange closed")
        
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
        
        DONE(0)
      }
      
      DONE_
    }
    PASS
  } // PARAM _canClose
  
}; // CONTRACT scrugex