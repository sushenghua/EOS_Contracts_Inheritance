#include <InheritClt.hpp>

//-----------------------------------------------------------------------------
// ------ actions

#define GLOBAL_FLAG_TABLE_SCOPE   0
#define GLOBAL_FLAG_TALBE_ROW_KEY 0

ACTION InheritClt::init() {
  require_auth( get_self() );
  GlobalFlagIndex globalFlags( get_self(), GLOBAL_FLAG_TABLE_SCOPE );
  check( globalFlags.find(GLOBAL_FLAG_TALBE_ROW_KEY) == globalFlags.end(), "already initialized" );
  globalFlags.emplace( get_self(), [&](auto& row) {
    row.key = GLOBAL_FLAG_TALBE_ROW_KEY;
    row.miningEnabled = false;
  });
}

ACTION InheritClt::allocate(const name& inheritor, const name& tokencontract, const asset& quantity,
                            uint32_t validFrom, uint32_t cdDuration, const string& remark) {
  // check auth, args
  check( get_self() != inheritor, "cannot assign to self" );
  require_auth( get_self() );
  check( is_account( inheritor ), "inheritor account does not exist" );
  check( is_account( tokencontract ), "token contract does not exist" );
  check( quantity.is_valid(), "invalid token quantity" );
  check( quantity.amount > 0, "you cannot assign 0 quantity of the token" );
  check( remark.size() <= 256, "remark should be no more than 256 bytes" );

  // check token existence
  AccountIndex tokenTable( tokencontract, get_self().value );
  auto tokenItr = tokenTable.find( quantity.symbol.code().raw() );
  check( tokenItr != tokenTable.end(), "token doesn't exist in the contract, or you don't own the token" );
  auto balance = tokenItr->balance;
  check( quantity.symbol == balance.symbol, "symbol precision mismatch" );

  // check allocation availability, update allocation
  AllocationIndex allocation( get_self(), tokencontract.value );
  auto allocationItr = allocation.find( quantity.symbol.code().raw() );
  bool allocatedBefore = ( allocationItr != allocation.end() );
  if ( !allocatedBefore ) {     // no allocation ever happened before
    check( quantity <= balance, "you cannot allocate quantity more than the amount you own" );
    allocation.emplace( get_self(), [&](auto& row) {
      row.allocated = quantity;
      row.unallocated = balance - quantity;
      row.transfered = quantity - quantity; // 0 
    });
  }
  else {
    check( balance >= allocationItr->allocated, "your allocation become invalid due to lack of available balance" );
  }

  // add new or update existing inheritance record
  InheritanceIndex inheritance( get_self(), inheritor.value );
  auto uniqueTknIndex = inheritance.get_index<"uniquetkn"_n>();
  auto inheritanceItr = uniqueTknIndex.find( static_cast<uint128_t>(tokencontract.value) << 64
                                             | quantity.symbol.code().raw() );
  asset delta = quantity;
  if ( inheritanceItr == uniqueTknIndex.end() ) {
    inheritance.emplace( get_self(), [&](auto& row) {
      row.id = inheritance.available_primary_key();
      row.state = EState::ACTIVE;
      row.willGet.quantity = quantity;
      row.willGet.contract = tokencontract;
      row.validFrom = validFrom;
      row.cdBeganTime = validFrom;
      row.cdDuration = cdDuration;
      row.remark = remark;
    });
  }
  else {
    delta -= inheritanceItr->willGet.quantity;
    uniqueTknIndex.modify( inheritanceItr, get_self(), [&](auto& row) {
      row.state = EState::ACTIVE;
      // contract and symbol keep unchanged, otherwise non-unique
      row.willGet.quantity = quantity;
      // row.willGet.contract = tokencontract;
      row.validFrom = validFrom;
      row.cdBeganTime = validFrom;
      row.cdDuration = cdDuration;
      row.remark = remark;
    });
  }

  // update allocation by delta change
  if ( allocatedBefore ) {
    check( delta <= allocationItr->unallocated, "you cannot allocate quantity more than available amount" );
    allocation.modify( allocationItr, get_self(), [&](auto& row) {
      row.allocated = allocationItr->allocated + delta;
      // row.unallocated = allocationItr->unallocated - delta;
      row.unallocated = balance - row.allocated;
    });
  }

  #ifdef DEBUG_PRINT
    allocationItr = allocation.find( quantity.symbol.code().raw() );
    print_f("[InheritClt::allocate] allocated : %, unallocated: %, transfered: %\n",
            allocationItr->allocated, allocationItr->unallocated, allocationItr->transfered);
  #endif
}

ACTION InheritClt::unallocate(const name& inheritor, const name& tokencontract, const symbol& sym) {
  // check auth, args
  require_auth( get_self() );
  check( is_account( inheritor ), "inheritor account does not exist" );
  check( is_account( tokencontract ), "token contract does not exist" );
  check( sym.is_valid(), "invalid token symbol" );

  // check previous allocation existence
  AllocationIndex allocation( get_self(), tokencontract.value );
  auto allocationItr = allocation.find( sym.code().raw() );
  check( allocationItr != allocation.end(), "no previous allocation found for the specified contract token" );

  // update inheritance table, allocation table
  InheritanceIndex inheritance( get_self(), inheritor.value );
  auto uniqueTknIndex = inheritance.get_index<"uniquetkn"_n>();
  auto inheritanceItr = uniqueTknIndex.find( static_cast<uint128_t>(tokencontract.value) << 64
                                             | sym.code().raw() );
  // auto inheritanceItr = inheritance.find( sym.code().raw() );
  check( inheritanceItr != uniqueTknIndex.end(), "no previous token allocation to the inheritor account found" );

  // update allocation table
  if ( allocationItr->allocated == inheritanceItr->willGet.quantity ) {
    allocation.erase(allocationItr);
  }
  else {
    allocation.modify( allocationItr, get_self(), [&](auto& row) {
      row.allocated = allocationItr->allocated - inheritanceItr->willGet.quantity;
      row.unallocated = allocationItr->unallocated + inheritanceItr->willGet.quantity;
    });
  }
  // erase row from inheritance table
  uniqueTknIndex.erase( inheritanceItr );

  #ifdef DEBUG_PRINT
    allocationItr = allocation.find( sym.code().raw() );
    if ( allocationItr != allocation.end() )
      print_f("[InheritClt::unallocate] allocated : %, unallocated: %, transfered: %\n",
              allocationItr->allocated, allocationItr->unallocated, allocationItr->transfered);
    else
      print_f("[InheritClt::unallocate] allocation erased for token contract: %, token: %\n", tokencontract, sym);
  #endif
}

ACTION InheritClt::freeze(const name& inheritor, const name& tokencontract, const symbol& sym) {
  // check auth, args
  require_auth( get_self() );
  check( is_account( inheritor ), "inheritor account does not exist" );
  check( is_account( tokencontract ), "token contract does not exist" );
  check( sym.is_valid(), "invalid token symbol" );

  // update inheritance table, allocation table
  InheritanceIndex inheritance( get_self(), inheritor.value );
  auto uniqueTknIndex = inheritance.get_index<"uniquetkn"_n>();
  auto inheritanceItr = uniqueTknIndex.find( static_cast<uint128_t>(tokencontract.value) << 64
                                             | sym.code().raw() );
  check( inheritanceItr != uniqueTknIndex.end(), "no previous allocation found for the specified contract token" );

  uniqueTknIndex.modify( inheritanceItr, get_self(), [&](auto& row) {
    row.state = EState::FROZEN;
  });

  #ifdef DEBUG_PRINT
    print_f("[InheritClt::freeze] inheritor : %, frozen asset: %\n", inheritor, inheritanceItr->willGet);
  #endif
}

ACTION InheritClt::setenable(bool enabled) {
  // check auth, args
  require_auth( get_self() );
  GlobalFlagIndex globalFlags( get_self(), GLOBAL_FLAG_TABLE_SCOPE );
  auto itr = globalFlags.find( GLOBAL_FLAG_TALBE_ROW_KEY );
  check ( itr != globalFlags.end(), "uninitialized contract" );
  if ( itr->miningEnabled != enabled ) {
    globalFlags.modify( itr, get_self(), [&](auto& row) {
      row.miningEnabled = enabled;
    });
  }
  #ifdef DEBUG_PRINT
    print_f("[InheritClt::setenable] %\n", enabled ? "enabled" : "disabled");
  #endif
}

//-----------------------------------------------------------------------------
// ------ action only can be called from inherit agent
const name ONLY_AGENT{"inheritagent"};

ACTION InheritClt::onagentmine(const name& inheritor, const name& tokencontract, const asset& quantity,
                               const name& assetclient, const name& miner) {
  // #ifdef DEBUG_PRINT
  //   print_f("[InheritClt::onagentmine] inheritor: %, token contract: %, quantity: %, miner: %; first_receiver: %\n",
  //           inheritor, tokencontract, quantity, miner, get_first_receiver());
  // #endif

  // check auth, args
  require_auth(ONLY_AGENT);
  check( get_self() == assetclient, "client mismatch" );
  // check( get_first_receiver() == ONLY_AGENT, "only accept notification from agent" ); // check if "on_notify" used
  check( _miningEnabled(), "mining disabled" );

  // find record in inheritance table
  InheritanceIndex inheritance( get_self(), inheritor.value );
  auto uniqueTknIndex = inheritance.get_index<"uniquetkn"_n>();
  auto inheritanceItr = uniqueTknIndex.find( static_cast<uint128_t>(tokencontract.value) << 64
                                             | quantity.symbol.code().raw() );
  check( inheritanceItr != uniqueTknIndex.end(), "no inheritance asset specified for the inheritor account" );
  check( inheritanceItr->state != EState::FROZEN, "this specified inheritance is frozen" );
  check( inheritanceItr->willGet.quantity == quantity, "quantity mismatched with willget-quantity" );

  do { // flow control
    uint32_t now = _timenow();
    if ( now >= inheritanceItr->cdBeganTime + inheritanceItr->cdDuration ) {  // CD or transfer mining
      if ( inheritanceItr->state == EState::ACTIVE ) {
        // update state to ACTIVECD_MINED
        uniqueTknIndex.modify( inheritanceItr, get_self(), [&](auto& row) {
          row.state = EState::ACTIVECD_MINED;
          row.cdBeganTime = now;
        });
        // notify agent the success of mining
        require_recipient(ONLY_AGENT);

        #ifdef DEBUG_PRINT
          print_f("[InheritClt::onagentmine] done CD mining, inheritor: %, token contract: %, quantity: %, cdBeganTime: %\n",
                  inheritor, tokencontract, quantity, now);
        #endif
      }
      else if ( inheritanceItr->state == EState::ACTIVECD_MINED ) {
        // check transfer table
        TransferedIndex transfered( get_self(), tokencontract.value );
        auto receiverTokenIndex = transfered.get_index<"rcvrtoken"_n>();
        auto transferedItr = receiverTokenIndex.find( static_cast<uint128_t>(inheritor.value) << 64
                                                      | quantity.symbol.code().raw() );
        if ( transferedItr != receiverTokenIndex.end()
             && transferedItr->got == inheritanceItr->willGet.quantity
             && transferedItr->validFrom == inheritanceItr->validFrom
             && transferedItr->cdDuration == inheritanceItr->cdDuration ) {   // --> repeated transfer mining
          #ifdef DEBUG_PRINT
            print_f("[InheritClt::onagentmine] repeated transfer mining invalid\n");
          #endif
          break;
        }

        // update allocation tokenTable                                       // --> transfer mining
        AllocationIndex allocation( get_self(), tokencontract.value );
        auto allocationItr = allocation.find( quantity.symbol.code().raw() );
        check( allocationItr != allocation.end(), "critical table un-sync error" );
        // if ( allocationItr == allocation.end() ) break;
        allocation.modify( allocationItr, get_self(), [&](auto& row) {
          row.allocated -= quantity;
          row.transfered += quantity;
        });

        // fire transfer action
        action(
          permission_level{ get_self(), "active"_n },
          inheritanceItr->willGet.contract,
          "transfer"_n,
          std::make_tuple(get_self(), inheritor, inheritanceItr->willGet.quantity, inheritanceItr->remark)
        ).send();

        // add record to the tranfered table
        transfered.emplace( get_self(), [&](auto& row) {
          row.id = transfered.available_primary_key();
          row.receiver = inheritor;
          row.got = inheritanceItr->willGet.quantity;
          row.validFrom = inheritanceItr->validFrom;
          row.cdBeganTime = inheritanceItr->cdBeganTime;
          row.cdDuration = inheritanceItr->cdDuration;
          row.transferedTime = _timenow();
          row.remark = inheritanceItr->remark;
        });

        // remove record from inheritance table
        uniqueTknIndex.erase( inheritanceItr );

        // notify agent the success of mining
        require_recipient(ONLY_AGENT);

        #ifdef DEBUG_PRINT
          print_f("[InheritClt::onagentmine] done Transfer mining, inheritor: %, token contract: %, quantity: %, transTime: %\n",
                  inheritor, tokencontract, quantity, now);
        #endif
      }
    }
    else if ( now >= inheritanceItr->validFrom ) {                            // --> active cd mine
      // check( inheritanceItr->state == EState::ACTIVE, "repeated CD mining invalid" );
      if ( inheritanceItr->state == EState::ACTIVE ) {
        // update state to ACTIVECD_MINED
        uniqueTknIndex.modify( inheritanceItr, get_self(), [&](auto& row) {
          row.state = EState::ACTIVECD_MINED;
          row.cdBeganTime = now;
        });
        // notify agent the success of mining
        require_recipient(ONLY_AGENT);

        #ifdef DEBUG_PRINT
          print_f("[InheritClt::onagentmine] done CD mining, inheritor: %, token contract: %, quantity: %, cdBeganTime: %\n",
                  inheritor, tokencontract, quantity, now);
        #endif
      }
      #ifdef DEBUG_PRINT
      else {
        print_f("[InheritClt::onagentmine] repeated CD mining invalid\n");
      }
      #endif
    }
    else {                                                                  // --> invalid mine
      // check ( false, "invalid mining due to unmeet condition" );
      #ifdef DEBUG_PRINT
        print_f("[InheritClt::onagentmine] miming failed due to unmet condition\n");
      #endif
      break;
    }

  } while (false); // end of flow control
}

//-----------------------------------------------------------------------------
// ------ private helper methods
bool InheritClt::_miningEnabled() const {
  GlobalFlagIndex globalFlags( get_self(), GLOBAL_FLAG_TABLE_SCOPE );
  auto itr = globalFlags.find( GLOBAL_FLAG_TALBE_ROW_KEY );
  if ( itr != globalFlags.end() )
    return itr->miningEnabled;
  else
    return false;
}

//-----------------------------------------------------------------------------
// ------ below define the action/function for debug only purpose
#ifdef DEBUG
ACTION InheritClt::clearinherit(const name& inheritor) {
  // check auth, args
  require_auth( get_self() );
  check( is_account( inheritor ), "inheritor account does not exist" );

  InheritanceIndex inheritance( get_self(), inheritor.value );
  auto itr = inheritance.begin();
  while ( itr != inheritance.end() ) {
    itr = inheritance.erase( itr );
  }
  #ifdef DEBUG_PRINT
    print_f("[InheritClt::clearinherit] clear inheritance for inheritor: %\n", inheritor);
  #endif
}

ACTION InheritClt::clearalloc(const name& tokencontract) {
  // check auth, args
  require_auth( get_self() );
  check( is_account( tokencontract ), "token contract does not exist" );

  AllocationIndex allocation( get_self(), tokencontract.value );
  auto itr = allocation.begin();
  while ( itr != allocation.end() ) {
    itr = allocation.erase( itr );
  }
  #ifdef DEBUG_PRINT
    print_f("[InheritClt::clearalloc] clear token allocation for token contract: %\n", tokencontract);
  #endif
}

ACTION InheritClt::cleartrans(const name& tokencontract) {
  // check auth, args
  require_auth( get_self() );
  check( is_account( tokencontract ), "token contract does not exist" );

  TransferedIndex trans( get_self(), tokencontract.value );
  auto itr = trans.begin();
  while ( itr != trans.end() ) {
    itr = trans.erase( itr );
  }
  #ifdef DEBUG_PRINT
    print_f("[InheritClt::clearalloc] clear token transfer table for token contract: %\n", tokencontract);
  #endif
}

ACTION InheritClt::printtime() {
  // check auth, args
  #ifdef DEBUG_PRINT
    print_f("[InheritClt::printtime] time: %\n", _timenow());
  #endif
}
#endif // DEBUG
