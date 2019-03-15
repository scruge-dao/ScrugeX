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

def newcampaign(eosioscrugex, founder,
	supply="100.0000 TEST", token="token",
	softCap="10.0000 EOS", hardCap="20.0000 EOS", initial=25,
	minContrib="0.0010 EOS", maxContrib=35, duration=8000, milestones=None):

	timestamp = int(time.time()*1000.0)
	SECOND = 1000

	if milestones == None:
		milestones = [
				{ "duration": 2 * SECOND, "fundsReleasePercent": 25 },
				{ "duration": 2 * SECOND, "fundsReleasePercent": 25 },
				{ "duration": 2 * SECOND, "fundsReleasePercent": 25 }]
	
	eosioscrugex.push_action("newcampaign",
		{
			"supplyForSale": supply,
			"tokenContract": token,
			"founderEosAccount": founder,
			"softCap": softCap,
			"hardCap": hardCap,
			"initialFundsReleasePercent": initial,
			"kycEnabled": False,
			"minUserContribution": minContrib,
			"maxUserContributionPercent": maxContrib, # 7 EOS
			"startTimestamp": timestamp + SECOND,
			"campaignDuration": duration,
			"milestones": milestones
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

# assert

def assertErrors(self, tests):
	for test in tests:
		if isinstance(test, list) and len(test) == 2:
			assertRaisesMessage(self, test[0], test[1])
		else:
			assertRaises(self, test)

def assertRaisesMessage(self, message, func):
	with self.assertRaises(Error) as c:
		func()
	self.assertIn(message, c.exception.message)

def assertRaises(self, func):
	with self.assertRaises(Error):
		func()