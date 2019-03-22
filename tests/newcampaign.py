# Copyright © Scruge 2019.
# This file is part of ScrugeX.
# Created by Yaroslav Erohin.

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
		SCENARIO("Test newcampaign action")
		reset()

		create_master_account("master")
		create_account("founder", master, "founder")

		# Token

		create_account("eosio_token", master, "eosio.token")
		create_account("token", master, "token")

		token_contract = Contract(eosio_token, "02_eosio_token")
		deploy(token_contract)

		token_contract = Contract(token, "02_eosio_token")
		deploy(token_contract)

		# ScrugeX
		
		key = CreateKey(is_verbose=False)
		create_account("eosioscrugex", master, "eosioscrugex", key)
		perm(eosioscrugex, key)

		contract = Contract(eosioscrugex, "ScrugeX/src/")
		deploy(contract)

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
			lambda: newcampaign(eosioscrugex, founder, softCap="20.0000 EOS", hardCap="10.000 EOS")],


			["milestone duration should be longer", 
			lambda: newcampaign(eosioscrugex, founder, 
				milestones=[{ "duration": 0, "fundsReleasePercent": 25 }]
			)],

			["milestone duration should be shorter",
			lambda: newcampaign(eosioscrugex, founder,  
				milestones=[{ "duration": 1 + 84 * 24 * 60 * 60 * 1000, "fundsReleasePercent": 25 }]
			)],

			["milestone funds release can not be higher than 25%",
			lambda: newcampaign(eosioscrugex, founder,  
				milestones=[{ "duration": 10000, "fundsReleasePercent": 26 }]
			)],

			["milestone funds release can not be 0%",
			lambda: newcampaign(eosioscrugex, founder,  
				milestones=[{ "duration": 10000, "fundsReleasePercent": 0 }]
			)],

			["total funds release can be less than 100%",
			lambda: newcampaign(eosioscrugex, founder,
				milestones=[{ "duration": 10000, "fundsReleasePercent": 25 }]
			)],

			["total funds release can not go over 100%",
			lambda: newcampaign(eosioscrugex, founder,
				milestones=[{ "duration": 10000, "fundsReleasePercent": 25 },
							{ "duration": 10000, "fundsReleasePercent": 25 },
							{ "duration": 10000, "fundsReleasePercent": 25 },
							{ "duration": 10000, "fundsReleasePercent": 25 },
							{ "duration": 10000, "fundsReleasePercent": 1 }]
			)]		
		])

		# Create correctly
		newcampaign(eosioscrugex, founder)




# main

if __name__ == "__main__":
	unittest.main()