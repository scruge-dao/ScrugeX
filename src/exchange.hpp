void scrugex::buy(name eosAccount, uint64_t campaignId, asset quantity, asset sum) {
  require_auth(eosAccount);
  
	// fetch campaign
	campaigns_i campaigns(_self, _self.value);
	auto campaignItem = campaigns.find(campaignId);
	eosio_assert(campaignItem != campaigns.end(), "campaign does not exist");

  uint64_t userId = _verify(eosAccount, campaignItem->kycEnabled);
  
  exchangeinfo_i exchangeinfo(_self, campaignId);
  auto exchangeItem = exchangeinfo.begin();
  eosio_assert(exchangeItem != exchangeinfo.end(), "exchange does not exist");
  eosio_assert(exchangeItem->status == ExchangeStatus::buying, "exchange doesn't take buy orders right now");
  
  eosio_assert(quantity.symbol.is_valid(), "invalid quantity");
  eosio_assert(quantity.symbol == exchangeItem->roundSellVolume.symbol, "incorrect quantity symbol");
  eosio_assert(quantity.amount > 0, "only positive quantity allowed");
  
  eosio_assert(sum.symbol.is_valid(), "invalid price");
  eosio_assert(sum.symbol == exchangeItem->investorsFund.symbol, "incorrect price symbol");
  eosio_assert(sum.amount > 0, "only positive sum allowed");

  double price = (double)sum.amount / (double)quantity.amount;
  
  eosio_assert(price > 0, "token price calculated with arguments passed is too low");
  
  eosio_assert(price <= exchangeItem->roundPrice,
    "token price calculated with arguments can not be higher than current auction price");
  
  // delete unpaid orders
  buyorders_i buyorders(_self, campaignId);
  auto buyordersIndex = buyorders.get_index<"byuserid"_n>();
  auto item = buyordersIndex.find(userId);
  while (item != buyordersIndex.end()) {
    if (!item->paymentReceived) {
      buyordersIndex.erase(item);
      break;
    }
    item++;
  }
  
  buyorders.emplace(eosAccount, [&](auto& r) {
    r.milestoneId = exchangeItem->milestoneId;
    r.key = buyorders.available_primary_key();
    r.userId = userId;
    r.quantity = quantity;
    r.sum = sum;
    r.price = price;
    r.eosAccount = eosAccount;
    r.attemptedPayment = false;
    r.isPaid = false;
    r.paymentReceived = false;
    r.timestamp = time_ms();
    r.purchased = asset(0, quantity.symbol);
    r.spent = 0;
  });
  
} // void scrugex::buy


void scrugex::sell(name eosAccount, uint64_t campaignId, asset quantity) {
  require_auth(eosAccount);
  
	// fetch campaign
	campaigns_i campaigns(_self, _self.value);
	auto campaignItem = campaigns.find(campaignId);
	eosio_assert(campaignItem != campaigns.end(), "campaign does not exist");

  uint64_t userId = _verify(eosAccount, campaignItem->kycEnabled);
  
  // to-do check if backer
  
  exchangeinfo_i exchangeinfo(_self, campaignId);
  auto exchangeItem = exchangeinfo.begin();
  eosio_assert(exchangeItem != exchangeinfo.end(), "exchange does not exist");
  eosio_assert(exchangeItem->status == ExchangeStatus::selling, "exchange doesn't take sell orders right now");
  
  eosio_assert(quantity.symbol.is_valid(), "invalid quantity");
  eosio_assert(quantity.symbol == exchangeItem->roundSellVolume.symbol, "incorrect symbol");
  eosio_assert(quantity.amount > 0, "invalid quantity");
  
  contributions_i contributions(_self, campaignId);
  auto contributionItem = contributions.find(userId);
  
  eosio_assert(contributionItem != contributions.end(), "you need to be an investor to sell tokens");
  
  // how many tokens will this contribution allows me
  uint64_t paymentAmount = (uint64_t) floor((double)campaignItem->supplyForSale.amount /
          (double)campaignItem->raised.amount * (double)contributionItem->quantity.amount);
  
  eosio_assert(paymentAmount >= quantity.amount, "can not sell more tokens that you have");
  
  exchangeinfo.modify(exchangeItem, same_payer, [&](auto& r) {
    r.roundSellVolume += quantity;
  });
  
  sellorders_i sellorders(_self, campaignId);
  auto sellordersIndex = sellorders.get_index<"byuserid"_n>();
  auto item = sellordersIndex.find(userId);
  
  // check all sell orders by this user and decline if one for this milestone exists
  while (item != sellordersIndex.end() && item->userId == userId) {
    if (item->milestoneId == exchangeItem->milestoneId) {
      eosio_assert(item == sellordersIndex.end(), "you can only create one sell order");
    }
    item++;
  }
  
  sellorders.emplace(eosAccount, [&](auto& r) {
    r.milestoneId = exchangeItem->milestoneId;
    r.key = sellorders.available_primary_key();
    r.userId = userId;
    r.eosAccount = eosAccount;
    r.quantity = quantity;
    r.timestamp = time_ms();
    r.attemptedPayment = false;
    r.isPaid = false;
    r.received = asset(0, exchangeItem->investorsFund.symbol);
  });
  
} // void scrugex::sell
