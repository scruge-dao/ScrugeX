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

		key = CreateKey(is_verbose=False)

		# Token

		create_account("eosio_token", master, "eosio.token")

		token_contract = Contract(eosio_token, "02_eosio_token")
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

		bounty_contract = Contract(eosioscrugex, "ScrugeX/src")
		if not bounty_contract.is_built():
			bounty_contract.build()
		bounty_contract.deploy()

		# Distribute tokens

		create_issue(eosio_token, master, "EOS")

		# Users

		create_account("founder", master, "founder")

		for s in investors:
			create_account(s, master, s)
			transfer(eosio_token, master, s, "1000000.0000 EOS", "")

	def run(self, result=None):
		super().run(result)

	# tests

	def test_create_project(self):
		newcampaign(eosioscrugex)

		i = 1
		for s in investors:
			transfer(eosio_token, s, eosioscrugex, "1000.0000 EOS", "%d-0"%i)
			i += 1

		refresh(eosioscrugex)

		sleep(10)

		refresh(eosioscrugex)

		sleep(1)

		# check table


# main

if __name__ == "__main__":
	unittest.main()