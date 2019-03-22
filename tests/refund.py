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
		SCENARIO("Test refund action")
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

		contract = Contract(eosioscrugex, "ScrugeX/src")
		deploy(contract)

		# Distribute tokens

		create_issue(eosio_token, master, "EOS")
		create_issue(token, founder, "TEST")

	def run(self, result=None):
		super().run(result)

	# tests

	def test(self):
		newcampaign(eosioscrugex, founder)

		transfer(token, founder, eosioscrugex, "100.0000 TEST", "0")

		sleep(2)

		self.assertEqual(0, table(eosioscrugex, "campaigns", element="status"))

		eosioscrugex.push_action("refund", { "campaignId": 0 }, 
			permission=[(founder, Permission.ACTIVE)])

		self.assertEqual(4, table(eosioscrugex, "campaigns", element="status"))


# main

if __name__ == "__main__":
	unittest.main()