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
		SCENARIO("Test campaign flow")
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

		transfer(eosio_token, investora, eosioscrugex, "6.5000 EOS", "0")
		transfer(eosio_token, investorb, eosioscrugex, "6.5000 EOS", "0")
		transfer(eosio_token, investorc, eosioscrugex, "6.5000 EOS", "0")
		transfer(eosio_token, investord, eosioscrugex, "6.5000 EOS", "0")

		cancel(eosioscrugex, investora)

		cancel(eosioscrugex, investorb)
		transfer(eosio_token, investorb, eosioscrugex, "6.5000 EOS", "0")

		cancel(eosioscrugex, investorc)
		transfer(eosio_token, investorc, eosioscrugex, "2.5000 EOS", "0")

		sleep(3)
		refresh(eosioscrugex)

		sleep(5)
		refresh(eosioscrugex)

		assertErrors(self, [

			["only investors can participate in the voting",	
			lambda: vote(eosioscrugex, investora, True)],

			["campaign has ended",
			lambda: cancel(eosioscrugex, investorb)]
		])

		vote(eosioscrugex, investorb, True)
		vote(eosioscrugex, investorc, True)
		vote(eosioscrugex, investord, True)

		# assert

		# 0.001 is the ram comission for a test node

		self.assertEqual(6.499, amount(table(eosioscrugex, "contribution", "0", row=0, element="quantity")))
		self.assertEqual(2.499, amount(table(eosioscrugex, "contribution", "0", row=1, element="quantity")))
		self.assertEqual(6.499, amount(table(eosioscrugex, "contribution", "0", row=2, element="quantity")))


# main

if __name__ == "__main__":
	unittest.main()