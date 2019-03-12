#include "scrugex.hpp"
#include "debug.cpp"
#include "helper.cpp"
#include "refresh.cpp"
#include "transfer.cpp"
#include "newcampaign.cpp"
#include "exchange.cpp"
#include "manage.cpp"
#include "payment.cpp"
#include "vote.cpp"

// dispatch

extern "C" {
	
	void apply(uint64_t receiver, uint64_t code, uint64_t action) {
		
		if (code == receiver) {
			switch (action) {
				EOSIO_DISPATCH_HELPER(scrugex,
						(newcampaign)(vote)(extend)(refresh)(send)(pay)(take)(refund)
            (buy)(sell) 
						(destroy))
			}
		}
		else if (action == "transfer"_n.value && code != receiver) {
			execute_action(name(receiver), name(code), &scrugex::transfer);
		}
	}
};