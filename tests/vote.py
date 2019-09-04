# coding: utf8
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
		SCENARIO("Test vote action")
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

		# Users

		for s in investors:
			create_account(s, master, s)
			transfer(eosio_token, master, s, "1000000.0000 EOS", "")

	def run(self, result=None):
		super().run(result)

	# tests

	def test(self):
		newcampaign(eosioscrugex, founder)
		transfer(token, founder, eosioscrugex, "100.0000 TEST", "0")

		sleep(2)

		transfer(eosio_token, investora, eosioscrugex, "2.5000 EOS", "0")
		transfer(eosio_token, investorb, eosioscrugex, "2.5000 EOS", "0")
		transfer(eosio_token, investorc, eosioscrugex, "2.5000 EOS", "0")
		transfer(eosio_token, investord, eosioscrugex, "2.5000 EOS", "0")
		transfer(eosio_token, investore, eosioscrugex, "2.5000 EOS", "0")

		assertRaisesMessage(self, "voting is not currently held", 
			lambda: vote(eosioscrugex, investora, True))

		sleep(5)
		refresh(eosioscrugex)

		sleep(5)
		refresh(eosioscrugex)

		assertErrors(self, [

			["campaign founder can not participate in the voting",
			lambda: vote(eosioscrugex, founder, True)],
			
			["only investors can participate in the voting",
			lambda: vote(eosioscrugex, master, True)]
		])

		vote(eosioscrugex, investora, True)
		vote(eosioscrugex, investorb, True)
		vote(eosioscrugex, investorc, True)
		vote(eosioscrugex, investord, False)
		vote(eosioscrugex, investore, False)

		sleep(5)
		refresh(eosioscrugex)

		self.assertEqual(0, table(eosioscrugex, "voting", scope="0", element="active"))
		self.assertEqual(3, table(eosioscrugex, "voting", scope="0", element="positiveVotes"))
		self.assertEqual(5, table(eosioscrugex, "voting", scope="0", element="voters"))
		self.assertEqual(1, table(eosioscrugex, "voting", scope="0", element="kind"))

		# voting #2

		vote(eosioscrugex, investora, False)
		vote(eosioscrugex, investorb, False)
		vote(eosioscrugex, investorc, False)
		vote(eosioscrugex, investord, True)
		vote(eosioscrugex, investore, True)

		sleep(5)
		refresh(eosioscrugex)

		self.assertEqual(0, table(eosioscrugex, "voting", scope="0", row=1, element="active"))
		self.assertEqual(2, table(eosioscrugex, "voting", scope="0", row=1, element="positiveVotes"))
		self.assertEqual(5, table(eosioscrugex, "voting", scope="0", row=1, element="voters"))
		self.assertEqual(1, table(eosioscrugex, "voting", scope="0", row=1, element="kind"))


# main

if __name__ == "__main__":
	unittest.main()