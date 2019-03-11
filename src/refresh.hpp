#define CHECK(x) tie(t, action) = (x)(campaignItem, campaigns); if (action == RefreshAction::pass) {} else if (action == RefreshAction::doneT) { nextRefreshTime = t; break; } else if (action == RefreshAction::done) { break; } else if (action == RefreshAction::skip) { continue; }

void scrugex::refresh() {
	require_auth(_self);
	
	campaigns_i campaigns(_self, _self.value);
	uint64_t t = 0, nextRefreshTime = REFRESH_PERIOD;
	RefreshAction action;

	for (auto& campaignItem: campaigns) {

		CHECK(_campaignOver)
		
		CHECK(_fundingComplete)
		
		CHECK(_waitingOver)

    CHECK(_isRefunding)
    
    CHECK(_runExchange)
    
		CHECK(_voting)
	}

	if (nextRefreshTime != 0) {
		_scheduleRefresh(nextRefreshTime);
	}
	
} // void scrugex::refresh
