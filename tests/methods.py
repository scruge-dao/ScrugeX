import unittest
from eosfactory.eosf import *
from datetime import datetime
import time

investors = ["investora", "investorb", "investorc", "investord", "investore", "investorf", "investorg"]

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

def newcampaign(eosioscrugex, founder):
	timestamp = int(time.time()*1000.0)
	SECOND = 1000

	eosioscrugex.push_action("newcampaign",
		{
			"supplyForSale": "100.0000 TEST",
			"tokenContract": "token",
			"founderEosAccount": "founder",
			"softCap": "10.0000 EOS",
			"hardCap": "20.0000 EOS",
			"initialFundsReleasePercent": 25,
			"kycEnabled": False,
			"minUserContribution": "0.0010 EOS",
			"maxUserContributionPercent": 35,
			"publicTokenPercent": 80,
			"startTimestamp": timestamp + 1000,
			"campaignDuration": 8 * SECOND,
			"milestones": [
				{
					"duration": 2 * SECOND,
					"fundsReleasePercent": 25
				},
				{
					"duration": 2 * SECOND,
					"fundsReleasePercent": 25
				},
				{
					"duration": 2 * SECOND,
					"fundsReleasePercent": 25
				}
			]
		},
		permission=[(founder, Permission.ACTIVE)])


def vote(eosioscrugex, account, vote):
	eosioscrugex.push_action("vote", 
		{
			"eosAccount": account,
			"campaignId": 0,
			"vote": vote
		},
		permission=[(account, Permission.ACTIVE)])

def send(eosioscrugex, account):
	eosioscrugex.push_action("send", 
		{
			"eosAccount": account,
			"campaignId": 0
		},
		permission=[(eosioscrugex, Permission.ACTIVE)])

def sell(eosioscrugex, account, quantity):
	eosioscrugex.push_action("sell", 
		{
			"eosAccount": account,
			"campaignId": 0,
			"quantity": quantity
		},
		permission=[(account, Permission.ACTIVE)])

def take(eosioscrugex, account):
	eosioscrugex.push_action("take", 
		{
			"eosAccount": account,
			"campaignId": 0
		},
		permission=[(account, Permission.ACTIVE)])

def buy(eosioscrugex, account, quantity, summ):
	eosioscrugex.push_action("buy", 
		{
			"eosAccount": account,
			"campaignId": 0,
			"quantity": quantity,
			"sum": summ
		},
		permission=[(account, Permission.ACTIVE)])

def refresh(eosioscrugex):
	eosioscrugex.push_action("refresh", 
		"[]",
		permission=[(eosioscrugex, Permission.ACTIVE)])

def balance(token, account):
	return float(token.table("accounts", account).json["rows"][0]["balance"].split(" ")[0])