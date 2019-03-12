// methods for debug purposes

void scrugex::destroy() {
	require_auth(_self);
	uint64_t scopes = _getCampaignsCount();

	information_i table(_self, _self.value);
	auto item = table.begin();
	while(item != table.end()) { item = table.erase(item); }

	campaigns_i campaigns(_self, _self.value);
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
		
		excessfunds_i excessfunds(_self, i);
		auto e_item = excessfunds.begin();
		while(e_item != excessfunds.end()) { e_item = excessfunds.erase(e_item); }
		
	  
		sellorders_i sellorders(_self, i);
		auto s_item = sellorders.begin();
		while(s_item != sellorders.end()) { s_item = sellorders.erase(s_item); }
		
		exchangeinfo_i exchangeinfo(_self, i);
		auto ex_item = exchangeinfo.begin();
		while(ex_item != exchangeinfo.end()) { ex_item = exchangeinfo.erase(ex_item); }
	
		buyorders_i buyorders(_self, i);
		auto b_item = buyorders.begin();
		while(b_item != buyorders.end()) { b_item = buyorders.erase(b_item); }
	}
	
} // void scrugex::destroy
