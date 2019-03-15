import unittest
from eosfactory.eosf import *
from methods import *
from time import sleep
import string

verbosity([Verbosity.INFO, Verbosity.OUT, Verbosity.TRACE, Verbosity.DEBUG])

CONTRACT_WORKSPACE = "ScrugeX"

# methods

class Test(unittest.TestCase):

	# setup

	@classmethod
	def tearDownClass(cls):
		stop()

	@classmethod
	def setUpClass(cls):
		SCENARIO("Test newproject action")
		reset()

		create_master_account("master")
		create_account("founder", master, "founder")

		key = CreateKey(is_verbose=False)

		# Token

		create_account("eosio_token", master, "eosio.token")
		create_account("token", master, "token")

		token_contract = Contract(eosio_token, "02_eosio_token")
		if not token_contract.is_built():
			token_contract.build()
		token_contract.deploy()

		token_contract = Contract(token, "02_eosio_token")
		if not token_contract.is_built():
			token_contract.build()
		token_contract.deploy()

		# ScrugeX

		create_account("eosioscrugex", master, "eosioscrugex", key)
		eosioscrugex.set_account_permission(
			Permission.ACTIVE,
			{
					"threshold" : 1,
					"keys" : [{ "key": key.key_public, "weight": 1 }],
					"accounts": [{
						"permission": {
							"actor": "eosioscrugex",
							"permission": "eosio.code"
						},
						"weight": 1
					}],
				},
			Permission.OWNER, (eosioscrugex, Permission.OWNER))

		contract = Contract(eosioscrugex, "ScrugeX/src")
		if not contract.is_built():
			contract.build()
		contract.deploy()

		# Distribute tokens

		create_issue(eosio_token, master, "EOS")
		create_issue(token, founder, "TEST")

		# Users

		for s in investors:
			create_account(s, master, s)
			transfer(eosio_token, master, s, "1000000.0000 EOS", "")

	def run(self, result=None):
		super().run(result)

	# tests

	def test(self):
		newcampaign(eosioscrugex, founder,
			supply="100.0000 TEST", token="token",
			softCap="10.0000 EOS", hardCap="20.0000 EOS", initial=25,
			minContrib="0.0010 EOS", maxContrib=35, duration=8000)
		
		transfer(token, founder, eosioscrugex, "100.0000 TEST", "0")

		sleep(2)

		for s in investors:
			transfer(eosio_token, s, eosioscrugex, "2.5000 EOS", "0")

		sleep(5)
		refresh(eosioscrugex)

		sleep(5)
		refresh(eosioscrugex)

		for s in investors:
			vote(eosioscrugex, s, True)

		refresh(eosioscrugex)

		sleep(3)
		refresh(eosioscrugex)
		
		sleep(5)
		refresh(eosioscrugex)

		for i in range(0, 3):
			sleep(2)
			refresh(eosioscrugex)

		# MILESTONE 2

		for s in investors:
			vote(eosioscrugex, s, True)

		sleep(5)
		refresh(eosioscrugex)

		# MILESTONE 3

		for s in investors:
			vote(eosioscrugex, s, True)

		refresh(eosioscrugex)
		
		sleep(5)
		refresh(eosioscrugex)

		# read updates

		sleep(20)
		refresh(eosioscrugex)

		sleep(1)
		eosioscrugex.table("information", eosioscrugex)
		eosioscrugex.table("campaigns", eosioscrugex)
		eosioscrugex.table("contribution", "0")
		eosioscrugex.table("voting", "0")

		# check leftover tokens	
		self.assertLess(balance(eosio_token, eosioscrugex), 0.01)
		self.assertLess(balance(token, eosioscrugex), 0.001)

		# check tokens received
		self.assertGreater(balance(token, investora), 14)



# main

if __name__ == "__main__":
	unittest.main()