#pragma once
#include <eosiolib/eosio.hpp>
#include <eosiolib/asset.hpp>
#include <string>

using namespace eosio;
using namespace std;

// METHODS

// todo can this be optimized?
vector<string> split(string str, string sep) {
  char* cstr = const_cast<char*>(str.c_str()); 
  char* current;
  vector<string> arr;
  current=strtok(cstr,sep.c_str());
  
  while(current!=NULL) {
    arr.push_back(current); 
    current=strtok(NULL,sep.c_str()); 
  }
  return arr;
}

uint64_t time_ms() {
  return current_time() / 1000;
}

uint64_t get_percent(uint64_t value, uint64_t percent) { 
  return value * percent / 100;
}
