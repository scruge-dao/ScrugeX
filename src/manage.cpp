void scrugex::extend(uint64_t campaignId) {
	campaigns_i campaigns(_self, _self.value);
	auto campaignItem = campaigns.find(campaignId);
	eosio_assert(campaignItem != campaigns.end(), "campaign does not exist");

	require_auth(campaignItem->founderEosAccount);
	
	_startvote(campaignId, VoteKind::extendDeadline);
	
} // void scrugex::extend


void scrugex::refund(uint64_t campaignId) {
  
	// fetch campaign
	campaigns_i campaigns(_self, _self.value);
	auto campaignItem = campaigns.find(campaignId);
	eosio_assert(campaignItem != campaigns.end(), "campaign does not exist");
	auto scope = campaignItem->campaignId;
	
	require_auth(campaignItem->founderEosAccount);
	_refund(campaignItem->campaignId);

} // void scrugex::refund
