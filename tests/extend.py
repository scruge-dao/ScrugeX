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
		SCENARIO("Test extend action")
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
		SECOND = 1000
		milestones = [
				{ "duration": 10 * SECOND, "fundsReleasePercent": 25 },
				{ "duration": 10 * SECOND, "fundsReleasePercent": 25 },
				{ "duration": 10 * SECOND, "fundsReleasePercent": 25 }]
		newcampaign(eosioscrugex, founder, milestones=milestones)
		transfer(token, founder, eosioscrugex, "100.0000 TEST", "0")

		sleep(2)

		for s in investors:
			transfer(eosio_token, s, eosioscrugex, "2.5000 EOS", "0")

		sleep(5)
		refresh(eosioscrugex)

		# extend campaign

		assertRaisesMessage(self, "Missing required authority",
			lambda: extend(eosioscrugex, investora))

		duration = table(eosioscrugex, "milestones", scope="0", element="duration")

		extend(eosioscrugex, founder)

		# vote to extend
		for s in investors:
			vote(eosioscrugex, s, True)

		sleep(5)
		refresh(eosioscrugex)
		
		table(eosioscrugex, "voting", scope="0")
		
		duration2 = table(eosioscrugex, "milestones", scope="0", element="duration")

		self.assertGreater(duration, 0)
		self.assertGreater(duration2, duration)

		# try extending again
		assertRaisesMessage(self, "this voting already exists",
			lambda: extend(eosioscrugex, founder))

		sleep(10)
		refresh(eosioscrugex)

		# vote for milestone 
		for s in investors:
			vote(eosioscrugex, s, True)

		# milestone 2
		
		duration = table(eosioscrugex, "milestones", scope="0", row=1, element="duration")

		extend(eosioscrugex, founder)

		for s in investors:
			vote(eosioscrugex, s, False)

		sleep(3)
		refresh(eosioscrugex)
		
		duration2 = table(eosioscrugex, "milestones", scope="0", row=1, element="duration")

		self.assertGreater(duration, 0)
		self.assertEqual(duration2, duration)

		# try extending again
		assertRaisesMessage(self, "this voting already exists",
			lambda: extend(eosioscrugex, founder))

		sleep(5)
		refresh(eosioscrugex)

		# vote for milestone 
		for s in investors:
			vote(eosioscrugex, s, True)



# main

if __name__ == "__main__":
	unittest.main()