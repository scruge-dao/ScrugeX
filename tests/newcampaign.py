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

		contract = Contract(eosioscrugex, "ScrugeX/src/")
		if not contract.is_built():
			contract.build()
		contract.deploy()

		# Distribute tokens

		create_issue(eosio_token, master, "EOS")
		create_issue(token, founder, "TEST")

	def run(self, result=None):
		super().run(result)

	# tests

	def test_newcampaign(self):
		
		assertErrors(self, [

			["hard cap should be higher than soft cap", 
			lambda: newcampaign(eosioscrugex, founder, softCap="20.0000 EOS")],
		
			["you can not raise money for EOS",
			lambda: newcampaign(eosioscrugex, founder, token="eosio.token")],
		
			["you can not raise money for EOS",
			lambda: newcampaign(eosioscrugex, founder, supply="10.0000 EOS")],

			["min contribution should not be lower than max",
			lambda: newcampaign(eosioscrugex, founder, minContrib="7.0000 EOS")],

			["cap symbols mismatch", 
			lambda: newcampaign(eosioscrugex, founder, softCap="20.0000 EOS", hardCap="10.000 EOS")]
		])




# main

if __name__ == "__main__":
	unittest.main()