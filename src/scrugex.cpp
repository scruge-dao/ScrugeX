#include "scrugex.hpp"

using namespace eosio;
using namespace std;

void scrugex::transfer(name from, name to, asset quantity, string memo) {
	if (to != _self) { return; }
	require_auth(from);

	// check transfer
	eosio_assert(quantity.symbol.is_valid(), "invalid quantity");
	eosio_assert(quantity.amount > 0, "only positive quantity allowed");
	eosio_assert(quantity.symbol == EOS_SYMBOL, "only SCR tokens allowed");
	eosio_assert(memo != "", "incorrectly formatted memo");

	// get scruge account and campaign id
	auto memo_array = split(memo, "-");
	eosio_assert(memo_array.size() == 2, "incorrectly formatted memo");
	auto userId = stoull(memo_array[0]);
	auto campaignId = stoull(memo_array[1]);

	// check if account is verified to make this contribution
	accounts_i accounts("scrugeverify"_n, 0);
	auto accountItem = accounts.find(userId);
	auto eosAccount = from;
	
	eosio_assert(accountItem != accounts.end(), 
		"this scruge account is not verified");
	eosio_assert(accountItem->eosAccount == eosAccount, 
		"this eos account is not associated with scruge account");

	// fetch campaign
	campaigns_i campaigns(_self, 0);
	auto campaignItem = campaigns.find(campaignId);
	eosio_assert(campaignItem != campaigns.end(), "campaign does not exist");

	auto scope = campaignItem->scope;
	auto isNotFounder = eosAccount != campaignItem->founderEosAccount && 
						userId != campaignItem->founderUserId;
	eosio_assert(isNotFounder, "campaign founder can not contribute");

	// check if campaign started and not finished
	uint64_t time = time_ms();
	auto isActive = time > campaignItem->startTimestamp && 
					time < campaignItem->endTimestamp &&
					campaignItem->status == Status::funding;

	eosio_assert(isActive, "campaign is not active at the moment");

	// check if allowed to back this amount
	asset previous = _getContributionQuantity(scope, userId);
	asset total = previous + quantity;

	eosio_assert(campaignItem->maxUserContribution >= total, 
		"you can not contribute this much");
	eosio_assert(campaignItem->minUserContribution <= total, 
		"you can not contribute this little");

	// to-do check campaign cap?

	// upsert contribution
	contributions_i contributions(_self, scope);
	auto contributionItem = contributions.find(eosAccount.value);

	// upsert
	if (contributionItem != contributions.end()) {
		auto inUse = contributionItem->userId == userId;
		eosio_assert(inUse, 
			"this eos account was used to contrubute by another scruge user");

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


void scrugex::newcampaign(
	uint64_t campaignId, uint64_t founderUserId, name founderEosAccount,
	asset softCap, asset hardCap, uint64_t initialFundsReleasePercent,
	asset maxUserContribution, asset minUserContribution,
	uint64_t publicTokenPercent, uint64_t tokenSupply,
	uint64_t startTimestamp, uint64_t endTimestamp,
	uint64_t annualInflationPercentStart, uint64_t annualInflationPercentEnd,
	vector<milestoneInfo> milestones) {

	require_auth(_self);

	// check for duplicate campaign
	campaigns_i campaigns(_self, 0);
	auto item = campaigns.find(campaignId);
	auto doesntexist = item == campaigns.end() || campaigns.begin() == campaigns.end();
	eosio_assert(doesntexist, "campaign with this id already exists");

	int count = _getCampaignsCount();
	int scope = count;

	_updateCampaignsCount(scope);

	// to-do validate arguments (make sure it's complete)
	eosio_assert(is_account(founderEosAccount),
	 	"founder's account does not exist");
	eosio_assert(softCap < hardCap, 
		"hard cap should be higher than soft cap");
	eosio_assert(tokenSupply > 100000, 
		"supply can not be lower than 100,000 tokens");
	eosio_assert(startTimestamp < endTimestamp,
	 	"campaign end can not be earlier than campaign start");
	eosio_assert(milestones.size() > 0,
	 	"no milestones passed");
	eosio_assert(annualInflationPercentStart <= annualInflationPercentEnd, 
		"incorrect range passed for annualInflationPercent");
	eosio_assert(initialFundsReleasePercent < 500000, 
		"initial funds release can not be higher than 50%");

	campaigns.emplace(_self, [&](auto& r) {
		r.campaignId = campaignId;
		r.founderUserId = founderUserId;
		r.founderEosAccount = founderEosAccount;
		r.softCap = softCap;
		r.hardCap = hardCap;
		r.publicTokenPercent = publicTokenPercent;
		r.tokenSupply = tokenSupply;
		r.initialFundsReleasePercent = initialFundsReleasePercent;
		r.maxUserContribution = maxUserContribution;
		r.minUserContribution = minUserContribution;
		r.startTimestamp = startTimestamp;
		r.endTimestamp = endTimestamp;
		r.scope = scope;
		r.raised = asset(0, EOS_SYMBOL);
		r.currentMilestone = 0;
		r.status = Status::funding;
		r.annualInflationPercentStart = annualInflationPercentStart;
		r.annualInflationPercentEnd = annualInflationPercentEnd;
	});

	// to-do sort milestone arguments by deadline

	milestones_i table(_self, scope);
	auto lastDeadline = endTimestamp;
	auto totalFundsRelease = initialFundsReleasePercent;

	for (auto milestone: milestones) {

		// to-do validate milestone arguments (make sure it's complete)
		eosio_assert(lastDeadline < milestone.deadline,
			"next milestone deadline should always come after previous");

		lastDeadline = milestone.deadline;
		totalFundsRelease += milestone.fundsReleasePercent;

		eosio_assert(totalFundsRelease <= 1000000,
			"total funds release can not go over 100%");

		table.emplace(_self, [&](auto& r) {
			r.id = table.available_primary_key();
			r.deadline = milestone.deadline;
			r.fundsReleasePercent = milestone.fundsReleasePercent;
		});
	}

	eosio_assert(totalFundsRelease == 1000000, 
		"total funds release can be less than 100%");
		
} // void scrugex::newcampaign


void scrugex::vote(name eosAccount, uint64_t userId, uint64_t campaignId, bool vote) {
	require_auth(eosAccount);

	// check if account is verified to make this vote
	accounts_i accounts("scrugeverify"_n, 0);
	auto accountItem = accounts.find(userId);

	eosio_assert(accountItem != accounts.end(), "this scruge account is not verified");
	eosio_assert(accountItem->eosAccount == eosAccount,
		"this eos account is not associated with scruge account");

	campaigns_i campaigns(_self, 0);
	auto campaignItem = campaigns.find(campaignId);
	eosio_assert(campaignItem != campaigns.end(), "campaign does not exist");

	eosio_assert(eosAccount != campaignItem->founderEosAccount,
					"campaign founder can not participate in the voting");

	eosio_assert(campaignItem->status == Status::activeVote,
					"voting is not currently held");

	auto scope = campaignItem->scope;
	auto quantity = _getContributionQuantity(scope, userId);
	eosio_assert(quantity.amount > 0, "only investors can participate in the voting");

	voting_i voting(_self, scope);
	eosio_assert(voting.begin() != voting.end(), "voting is not currently held");

	auto votingItem = voting.begin();
	while (votingItem != voting.end()) {
		if (votingItem->active) { break; }
		votingItem++;
	}

	// to-do check if should maybe start the voting

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
	
} // void scrugex::vote


void scrugex::extend(uint64_t campaignId) {
	campaigns_i campaigns(_self, 0);
	auto campaignItem = campaigns.find(campaignId);
	eosio_assert(campaignItem != campaigns.end(), "campaign does not exist");

	require_auth(campaignItem->founderEosAccount);
	_startvote(campaignId, 0);
} // void scrugex::extend


void scrugex::refresh() {
	require_auth(_self);

	auto now = time_ms();
	campaigns_i campaigns(_self, 0);

	for (auto& campaignItem: campaigns) {

		if (now < campaignItem.endTimestamp || 			// still gathering money
			campaignItem.status == Status::closed ||	// over
			campaignItem.status == Status::waiting) {	// or waiting for input to-do how long do we wait

			continue;
		}

		if (campaignItem.raised < campaignItem.softCap) {

			// close campaign
			campaigns.modify(campaignItem, same_payer, [&](auto& r) {
				r.status = Status::closed;
			});

			// to-do return money

			continue;
		}

		if (campaignItem.status == Status::funding) {

			// starting milestones now
			campaigns.modify(campaignItem, same_payer, [&](auto& r) {
				r.status = Status::milestone;
			});

			// release initial funds
			auto percent = get_percent(campaignItem.raised.amount, 
									   campaignItem.initialFundsReleasePercent);

			auto quantity = campaignItem.raised;
			quantity.amount = percent;
			_transfer(campaignItem.founderEosAccount, quantity, "scruge: initial funds");

			continue; // to-do probably should be here for optimization, but does some harm in debug?
		}

		auto scope = campaignItem.scope;

		milestones_i milestones(_self, scope);
		auto milestoneId = campaignItem.currentMilestone;
		auto currentMilestoneItem = milestones.find(milestoneId);
		voting_i voting(_self, scope);

		// check extend deadline votings
		for (auto& votingItem: voting) { // to-do improve search for active voting
			if (votingItem.milestoneId == milestoneId &&
				votingItem.active &&
				votingItem.kind == 0) {

				// to-do check if can close vote earlier

				if (votingItem.endTimestamp < now || votingItem.voters == campaignItem.backersCount) {
					_stopvote(campaignItem.campaignId);

					if (votingItem.positiveVotes >= votingItem.voters * T2) {

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
					}
				}
				break;
			}
		}

		if (currentMilestoneItem->deadline < now) {
		
			// check if voting for deadline is over
			bool shouldStartNextVote = true;
			for (auto& votingItem: voting) { // to-do improve search for active voting
				if (votingItem.milestoneId == milestoneId &&
					votingItem.active &&
					votingItem.kind == 1) {

					shouldStartNextVote = false;
					
					// to-do check if can close vote earlier
					
					if (votingItem.endTimestamp < now || votingItem.voters == campaignItem.backersCount) {
						_stopvote(campaignItem.campaignId);
						
						if (votingItem.positiveVotes >= votingItem.voters * T1) {

							// release part of money
							auto percent = get_percent(campaignItem.raised.amount, 
													   currentMilestoneItem->fundsReleasePercent);
							auto quantity = campaignItem.raised;
							quantity.amount = percent;
							_transfer(campaignItem.founderEosAccount, quantity, 
								"scruge: milestone payment");

							// next milestone
							uint64_t nextMilestoneId = campaignItem.currentMilestone + 1;
							auto nextMilestoneItem = milestones.find(nextMilestoneId);

							if (nextMilestoneItem == milestones.end()) {

								// campaign ended successfully
								campaigns.modify(campaignItem, same_payer, [&](auto& r) {
									r.status = Status::closed;
								});
							}
							else {
								campaigns.modify(campaignItem, same_payer, [&](auto& r) {
									r.currentMilestone = nextMilestoneId;
									r.status = Status::milestone;
								});
							}
						}
						else {
							// deadline vote failed
							// wait for some time for founder to return funds or start extend voting
							campaigns.modify(campaignItem, same_payer, [&](auto& r) {
								r.status = Status::waiting;
							});
						}
					}
					break;
				}
			}

			if (shouldStartNextVote) {
				_startvote(campaignItem.campaignId, 1);
			}
		}
	}

	transaction t{};
  t.actions.emplace_back(permission_level(_self, "active"_n),
            					   _self, "refresh"_n,
            					   make_tuple());
  t.delay_sec = 300;
  t.send(time_ms(), _self, true);
  
} // void scrugex::refresh


void scrugex::send(name eosAccount) {
  require_auth(_self);
  
  // find in contributions
  
  // remove from contributions
  
  // inline send money to user
  _transfer(eosAccount, asset(1, EOS_SYMBOL), "scrugex refund for campaign -name-");
  
} // void scrugex::send


void scrugex::pay(uint64_t campaignId) {
  require_auth(_self);
  
  // assert isRefunding == true
  
  // get first in contributions
  
  // if doesn't exist, set isRefunding to false
  
  // else 
  
  // modify transfer_tries ++
  
  // schedule deferred actions
  
  {
    transaction t{};
    t.actions.emplace_back(permission_level(_self, "active"_n),
              					   _self, "send"_n,
              					   make_tuple(  ));
    t.delay_sec = 1;
    t.send(time_ms(), _self, true);
  }
  
  {
    transaction t{};
    t.actions.emplace_back(permission_level(_self, "active"_n),
              					   _self, "pay"_n,
              					   make_tuple( campaignId ));
    t.delay_sec = 2;
    t.send(time_ms(), _self, true);
  }

} // void scrugex::startrefund


// debug 


void scrugex::destroy() {
	require_auth(_self);
	uint64_t scopes = _getCampaignsCount();

	information_i table(_self, 0);
	auto item = table.begin();
	while(item != table.end()) { item = table.erase(item); }

	campaigns_i campaigns(_self, 0);
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

