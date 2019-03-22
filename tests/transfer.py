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
		SCENARIO("Test transfer action")
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

		# Investors

		create_account("investora", master, "investora")
		transfer(eosio_token, master, investora, "1000000.0000 EOS", "")
		transfer(eosio_token, master, founder, "10000.0000 EOS", "")
		transfer(token, founder, investora, "1000000.0000 TEST", "")


	def run(self, result=None):
		super().run(result)

	# tests

	def test_invest(self):

		newcampaign(eosioscrugex, founder, duration=60000, start=timeMs() + 6000) # 0

		# Test tokens transfer

		assertErrors(self, [

			["you have to use the contract specified", 
			lambda: transfer(eosio_token, founder, eosioscrugex, "100.0000 EOS", "0")],

			["you have to transfer specified amount for sale", 
			lambda: transfer(token, founder, eosioscrugex, "101.0000 TEST", "0")]
		])

		# Transfer correctly
		transfer(token, founder, eosioscrugex, "100.0000 TEST", "0")

		# Transfer again

		assertRaisesMessage(self, "you have already locked transferred tokens", 
		lambda: transfer(token, founder, eosioscrugex, "100.0000 TEST", "0"))

		# Other campaigns

		assertRaisesMessage(self, "campaign has not started yet", 
		lambda: transfer(eosio_token, investora, eosioscrugex, "2.5000 EOS", "0"))

		newcampaign(eosioscrugex, founder, supply="100.000 TEST") # 1, incorrect supply

		assertRaisesMessage(self, "supply symbol mismatch", 
		lambda: transfer(token, founder, eosioscrugex, "100.0000 TEST", "1"))

		newcampaign(eosioscrugex, founder, duration=3000) # 2
		transfer(token, founder, eosioscrugex, "100.0000 TEST", "2")

		# Investments

		assertErrors(self, [

			["campaign has not been supplied with tokens to sell", 
			lambda: transfer(eosio_token, investora, eosioscrugex, "2.5000 EOS", "1")],

			["incorrectly formatted memo",
			lambda: transfer(eosio_token, investora, eosioscrugex, "2.5000 EOS", "")],
			
			["campaignId is a number",
			lambda: transfer(eosio_token, investora, eosioscrugex, "2.5000 EOS", "a")],

			["campaign does not exist",
			lambda: transfer(eosio_token, investora, eosioscrugex, "2.5000 EOS", "999")],

			["you have to use the system EOS token",
			lambda: transfer(token, investora, eosioscrugex, "2.5000 TEST", "0")],

			["you can not contribute this much",
			lambda: transfer(eosio_token, investora, eosioscrugex, "999.5000 EOS", "0")],

			["you can not contribute such small amount",
			lambda: transfer(eosio_token, investora, eosioscrugex, "0.0001 EOS", "0")],

			["campaign has ended",
			lambda: transfer(eosio_token, investora, eosioscrugex, "2.5000 EOS", "2")]
		])

		# Invest correctly
		transfer(eosio_token, investora, eosioscrugex, "6.0000 EOS", "0")
		
		# Add to investment
		transfer(eosio_token, investora, eosioscrugex, "0.0001 EOS", "0")

		# Cancel investment
		cancel(eosioscrugex, investora, 0)

		# Invest again
		transfer(eosio_token, investora, eosioscrugex, "6.0000 EOS", "0")
		
		assertRaisesMessage(self, "you can not contribute this much", 
		lambda: transfer(eosio_token, investora, eosioscrugex, "1.1000 EOS", "0"))




# main

if __name__ == "__main__":
	unittest.main()