#define R_CHECK(x) tie(t, action) = (x)(campaignItem, campaigns); if (action == RefreshAction::pass) {} else if (action == RefreshAction::doneT) { nextRefreshTime = t; break; } else if (action == RefreshAction::done) { break; } else if (action == RefreshAction::skip) { continue; }

void scrugex::refresh() {
	require_auth(_self);
	
	INIT
	
	campaigns_i campaigns(_self, _self.value);
	for (auto& campaignItem: campaigns) {

		R_CHECK(_campaignOver)
		
		R_CHECK(_fundingComplete)
		
		R_CHECK(_waitingOver)

    R_CHECK(_isRefunding)
    
    R_CHECK(_runExchange)
    
		R_CHECK(_voting)
	}

	if (nextRefreshTime != 0) {
		_scheduleRefresh(nextRefreshTime);
	}
	
} // void scrugex::refresh
