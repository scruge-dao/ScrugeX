// Copyright © Scruge 2019.
// This file is part of ScrugeX.
// Created by Yaroslav Erohin.

#include "scrugex.hpp"
#include "debug.hpp"
#include "helper.hpp"
#include "refresh.hpp"
#include "transfer.hpp"
#include "newcampaign.hpp"
#include "manage.hpp"
#include "payment.hpp"
#include "vote.hpp"

// dispatch

extern "C" {
	
	void apply(uint64_t receiver, uint64_t code, uint64_t action) {
		
		if (code == receiver) {
			switch (action) {
				EOSIO_DISPATCH_HELPER(scrugex,
						(newcampaign)(extend)(refund)
						(vote)(send)(pay)(take)(cancel)
						(buyram)(pause)(refresh)
						(zdestroy))
			}
		}
		else if (action == "transfer"_n.value && code != receiver) {
			execute_action(name(receiver), name(code), &scrugex::transfer);
		}
	}
};