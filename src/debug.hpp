// methods for debug purposes

void scrugex::zdestroy() {
	require_auth(_self);
	
	// get campaigns count
	information_i information(_self, _self.value);
	auto infoItem = information.begin();
	uint64_t scope = 1;
	if (information.begin() != information.end()) {
	  scope = infoItem->campaignsCount;
	}

	information_i table(_self, _self.value);
	auto item = table.begin();
	while(item != table.end()) { item = table.erase(item); }

	campaigns_i campaigns(_self, _self.value);
	auto ca_item = campaigns.begin();
	while(ca_item != campaigns.end()) { ca_item = campaigns.erase(ca_item); }

	for (int i=0; i < scope; i++) {
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
	}
	
} // void scrugex::destroy
