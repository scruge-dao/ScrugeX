import unittest
from eosfactory.eosf import *
from datetime import datetime
import time

investors = ["investora", "investorb", "investorc", "investord", "investore"]

# methods

def create_issue(contract, to, symbol):
	contract.push_action("create",
		{
			"issuer": to,
			"maximum_supply": "1000000000.0000 {}".format(symbol)
		},
		permission=[(contract, Permission.ACTIVE)])
	contract.push_action("issue",
		{
			"to": to,
			"quantity": "1000000000.0000 {}".format(symbol),
			"memo": ""
		},
		permission=[(to, Permission.ACTIVE)])

def transfer(contract, fromAccount, to, quantity, memo):
	contract.push_action("transfer",
		{
			"from": fromAccount,
			"to": to,
			"quantity": quantity,
			"memo": memo
		},
		permission=[(fromAccount, Permission.ACTIVE)])

def newcampaign(eosioscrugex):
	timestamp = int(time.time()*1000.0)
	SECOND = 1000

	eosioscrugex.push_action("newcampaign",
		{
			"campaignId": 0,
			"founderUserId": 0, 
			"founderEosAccount": "founder",
			"softCap": "250000.0000 EOS",
			"hardCap": "500000.0000 EOS",
			"initialFundsReleasePercent": 490000,
			"minUserContribution": "10.0000 EOS",
			"maxUserContribution": "200000.0000 EOS",
			"publicTokenPercent": 800000,
			"tokenSupply": 1000000000,
			"startTimestamp": timestamp,
			"endTimestamp": timestamp + 10 * SECOND,
			"annualInflationPercentStart": 0,
			"annualInflationPercentEnd": 0,
			"milestones": [
				{
					"deadline": timestamp + 20 * SECOND,
					"fundsReleasePercent": 260000
				},
				{
					"deadline": timestamp + 40 * SECOND,
					"fundsReleasePercent": 250000
				}
			]
		},
		permission=[(eosioscrugex, Permission.ACTIVE)])


def refresh(eosioscrugex):
	eosioscrugex.push_action("refresh", 
		"[]",
		permission=[(eosioscrugex, Permission.ACTIVE)])
