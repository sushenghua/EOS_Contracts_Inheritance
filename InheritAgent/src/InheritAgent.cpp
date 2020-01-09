#include <InheritAgent.hpp>

//-----------------------------------------------------------------------------
// ------ actions

#ifdef DEBUG
#define EOSIOTOKEN "SYS"
#else
#define EOSIOTOKEN "EOS"
#endif

const uint8_t  ALLOWED_MINING_TRY_COUNT = 3;
const uint32_t FREE_TRY_CD_DURATION = 3600 * 24;                // 1 day
const asset MINING_FINE{1000, symbol{EOSIOTOKEN, 4}};           // 0.1 EOS
const asset CLIENT_SERVICE_COST{50000, symbol{EOSIOTOKEN, 4}};  // 5 EOS
const asset MINING_REWARD{10000, symbol{EOSIOTOKEN, 4}};        // 1 EOS
// const asset CD_MINING_REWARD{10000, symbol{EOSIOTOKEN, 4}};
// const asset TR_MINING_REWARD{10000, symbol{EOSIOTOKEN, 4}};
#define CD_MINING_REWARD  MINING_REWARD
#define TR_MINING_REWARD  MINING_REWARD

#define SELF_VAR_TABLE_SCOPE   0
#define SELF_VAR_TALBE_ROW_KEY 0

ACTION InheritAgent::init() {
  require_auth( get_self() );
  SelfVarIndex selfVar( get_self(), SELF_VAR_TABLE_SCOPE );
  check( selfVar.find(SELF_VAR_TALBE_ROW_KEY) == selfVar.end(), "already initialized" );
  selfVar.emplace( get_self(), [&](auto& row) {
    row.key = SELF_VAR_TALBE_ROW_KEY;
    row.enabled = true;
    row.earnings = MINING_FINE - MINING_FINE;
  });
}

void InheritAgent::_earn(const asset& quantity) {
  SelfVarIndex selfVar( get_self(), SELF_VAR_TABLE_SCOPE );
  auto varItr = selfVar.find(SELF_VAR_TALBE_ROW_KEY);
  check( varItr != selfVar.end(), "uninitialized agent contract" );
  selfVar.modify( varItr, get_self(), [&](auto& row) {
    row.earnings += quantity;
  });
}

ACTION InheritAgent::selfclaim(const name& to) {
  check( is_account( to ), "receiver account does not exist" );
  require_auth( get_self() );

  SelfVarIndex selfVar( get_self(), SELF_VAR_TABLE_SCOPE );
  auto varItr = selfVar.find(SELF_VAR_TALBE_ROW_KEY);
  check( varItr != selfVar.end(), "uninitialized agent contract" );

  asset quantity = varItr->earnings;
  string msg = "claim agent's total earnings: " + quantity.to_string();

  selfVar.modify( varItr, get_self(), [&](auto& row) {
    row.earnings.amount = 0;
  });

  // fire transfer action
  action(
    permission_level{ get_self(), "active"_n },
    "eosio.token"_n,
    "transfer"_n,
    make_tuple( get_self(), to, quantity, msg )
  ).send();
}

typedef enum {
  MiningFine      = 0,
  MiningReward    = 1,
  CDMiningReward  = 2,
  TRMiningReward  = 3,
  ClientService   = 4,
  // deposit/claim (eosio.token transfer) will not be recorded in bill table
  MinerDeposit    = 5,
  MinerClaim      = 6,
  ClientDeposit   = 7,
  ClientClaim     = 8
} BillType;

ACTION InheritAgent::mine(const name& inheritor, const name& tokencontract, const asset& quantity,
                          const name& assetclient, const name& miner) {
  // check auth, args
  require_auth(miner);
  check( is_account( inheritor ), "inheritor account does not exist" );
  check( is_account( tokencontract ), "token contract does not exist" );
  check( is_account( assetclient ), "asset client account does not exist");
  check( is_account( miner ), "miner account does not exist" );
  check( inheritor != assetclient, "client cannot be the inheritor" );
  check( assetclient != miner, "client cannot be the miner" );
  check( quantity.is_valid(), "invalid token quantity" );
  check( quantity.amount > 0, "invalid token quantity" );

  // check miner data: miner should deposit anti-attack charge
  MinerDataIndex minerData( get_self(), get_self().value );
  auto minerDataItr = minerData.find( miner.value );
  check( minerDataItr != minerData.end(), "to avoid malicious attack, mining requires at least 0.1 EOS" );
  check( minerDataItr->deposit >= MINING_FINE, "to avoid malicious attack, mining requires at least 0.1 EOS" );

  // check client data: client should deposit inheritance service charge
  ClientDataIndex clientData( get_self(), get_self().value );
  auto clientDataItr = clientData.find( assetclient.value );
  check( clientDataItr != clientData.end(), "no inheritance specified by this client" );
  check( clientDataItr->deposit >= CLIENT_SERVICE_COST, "the client has not deposit service fee yet" );

  uint32_t now = _timenow();
  bool miningAllowd = true;
  if ( minerDataItr->tryCount < ALLOWED_MINING_TRY_COUNT ) {
    minerData.modify( minerDataItr, get_self(), [&](auto& row) {
      row.tryCount += 1;
      row.lastTryTime = now;
    });
  }
  else if ( now > minerDataItr->lastTryTime + FREE_TRY_CD_DURATION ) {
    minerData.modify( minerDataItr, get_self(), [&](auto& row) {
      row.tryCount = 1;
      row.lastTryTime = now;
    });
  }
  else {    // punish miner for mining repeatedly and frequently
    minerData.modify( minerDataItr, get_self(), [&](auto& row) {
      row.deposit -= MINING_FINE;
      row.fee += MINING_FINE;
      row.tryCount = 0; // reset try count
    });
    MinerBillIndex minerBill( get_self(), get_self().value );
    minerBill.emplace( get_self(), [&](auto& row) {
      row.id = minerBill.available_primary_key();
      row.payer = miner;
      row.payee = get_self();
      row.quantity = -MINING_FINE;
      row.type = BillType::MiningFine;
      row.date = now;
    });
    _earn(MINING_FINE);
    miningAllowd = false;
    #ifdef DEBUG_PRINT
      print_f("[InheritAgent::mine] repeatedly mining got fine: %\n", MINING_FINE);
    #endif
  }

  if ( miningAllowd ) {
    // fire "mine" action in assetclient contract
    action(
      permission_level{ get_self(), "active"_n },
      assetclient,
      "onagentmine"_n,
      make_tuple( inheritor, tokencontract, quantity, assetclient, miner )
    ).send();

    // notify assetclient
    // require_recipient(assetclient);
    #ifdef DEBUG_PRINT
      print_f("[InheritAgent::mine] call onagentmine action   first receiver: %; inheritor: %, token contract: %, quantity: %\n",
              get_first_receiver(), inheritor, tokencontract, quantity);
    #endif
  }
}

// inheritance contract record state (copied from InheritClt class)
typedef enum {
  FROZEN          = 0,
  ACTIVE          = 1,
  ACTIVECD_MINED  = 2,
  TRANSFER_MINED  = 3
} InheritanceState;

void InheritAgent::didmine(const name& inheritor, const name& tokencontract, const asset& quantity,
                           const name& assetclient, const name& miner) {
  // require_auth( assetclient );
  check( get_first_receiver() == assetclient, "only accept notification from client" );

  MinerDataIndex minerData( get_self(), get_self().value );
  auto minerDataItr = minerData.find( miner.value );

  ClientDataIndex clientData( get_self(), get_self().value );
  auto clientDataItr = clientData.find( assetclient.value );

  // check( clientDataItr != clientData.end(), "no inheritance specified by this client" );
  // check( clientDataItr->deposit >= CLIENT_SERVICE_COST, "the client has not deposit service fee yet" );
  if ( minerDataItr != minerData.end() && clientDataItr != clientData.end()
       && clientDataItr->deposit >= CLIENT_SERVICE_COST && minerDataItr->deposit.amount > 0 ) {

    InheritanceIndex clientInheritance( assetclient, inheritor.value );
    auto uniqueTknIndex = clientInheritance.get_index<"uniquetkn"_n>();
    auto inheritanceItr = uniqueTknIndex.find( static_cast<uint128_t>(tokencontract.value) << 64
                                               | quantity.symbol.code().raw() );
    uint32_t now = _timenow();
    auto minerReward = CD_MINING_REWARD;
    auto minerBillType = BillType::CDMiningReward;

    #ifdef DEBUG_PRINT
    print_f("[InheritAgent::didmine] ===> inheritance found: %, state: %, is ACTIVECD_MINED: %\n",
            inheritanceItr != uniqueTknIndex.end()? "Yes":"No", inheritanceItr->state,
            inheritanceItr->state == InheritanceState::ACTIVECD_MINED? "Yes":"No");
    #endif

    if ( inheritanceItr != uniqueTknIndex.end() &&
         inheritanceItr->state == InheritanceState::ACTIVECD_MINED ) {  // --> CD mining
      // charge client for service: deduce charge amount from refund (CD mining)
      clientData.modify( clientDataItr, get_self(), [&](auto& row) {
        row.refund -= CLIENT_SERVICE_COST;
        row.fee += CLIENT_SERVICE_COST;
      });

      ClientBillIndex clientBill( get_self(), get_self().value );
      clientBill.emplace( get_self(), [&](auto& row) {
        row.id = clientBill.available_primary_key();
        row.payer = assetclient;
        row.payee = get_self();
        row.quantity = -CLIENT_SERVICE_COST;
        row.type = BillType::ClientService;
        row.date = now;
      });

      _earn(CLIENT_SERVICE_COST - CD_MINING_REWARD - TR_MINING_REWARD);
    }
    else {                                                              // --> TR mining
      // update to TR mining reward and type
      minerReward = TR_MINING_REWARD;
      minerBillType = BillType::TRMiningReward;

      // update client data: update deposit by decucing charge amount (TR mining)
      clientData.modify( clientDataItr, get_self(), [&](auto& row) {
        row.deposit -= CLIENT_SERVICE_COST;
      });
    }

    // update mining reward for the miner accordingly
    minerData.modify(minerDataItr, get_self(), [&](auto& row) {
      row.reward += minerReward;
      row.tryCount = 0;
    });

    MinerBillIndex minerBill( get_self(), get_self().value );
    minerBill.emplace( get_self(), [&](auto& row) {
      row.id = minerBill.available_primary_key();
      row.payer = get_self();
      row.payee = miner;
      row.quantity = minerReward;
      row.type = minerBillType;
      row.date = now;
    });
  }
}

ACTION InheritAgent::minerclaim(const name& miner) {
  // --> Note: deposit/claim (eosio.token transfer) will not record in agent
  // check auth, args
  check( is_account( miner ), "miner account does not exist" );
  require_auth( miner );

  // check miner data
  MinerDataIndex minerData( get_self(), get_self().value );
  auto minerDataItr = minerData.find( miner.value );
  check( minerDataItr != minerData.end(), "the miner is not found in agent" );

  // save the claim asset and memo msg
  asset quantity = minerDataItr->deposit + minerDataItr->reward;
  check( quantity.amount > 0, "the miner has nothong to claim" );

  string msg = "reward: " + minerDataItr->reward.to_string() + ", deposit refund: " + minerDataItr->deposit.to_string();
  uint32_t now = _timenow();

  // update mining reward for the miner
  minerData.modify(minerDataItr, get_self(), [&](auto& row) {
    row.deposit.amount = 0;
    row.reward.amount = 0;
    row.lastClaimTime = now;
  });

  // fire transfer action
  action(
    permission_level{ get_self(), "active"_n },
    "eosio.token"_n,
    "transfer"_n,
    make_tuple( get_self(), miner, quantity, msg )
  ).send();

  #ifdef DEBUG_PRINT
    print_f("[InheritAgent::minerclaim] %\n", msg);
  #endif
}

ACTION InheritAgent::clientclaim(const name& client) {
  // --> Note: deposit/claim (eosio.token transfer) will not record in agent
  // check auth, args
  check( is_account( client ), "client account does not exist" );
  require_auth( client );

  // check client data
  ClientDataIndex clientData( get_self(), get_self().value );
  auto clientDataItr = clientData.find( client.value );
  check( clientDataItr != clientData.end(), "the client is not found in agent" );
  check( clientDataItr->refund.amount > 0, "the client has nothing to claim" );

  // claim asset and memo msg
  asset quantity = clientDataItr->refund;
  string msg = "deposit refund: " + quantity.to_string();
  uint32_t now = _timenow();

  // update client data
  clientData.modify( clientDataItr, get_self(), [&](auto& row) {
    row.deposit.amount = 0;
    row.refund.amount = 0;
    row.lastClaimTime = now;
  });

  // fire transfer action
  action(
    permission_level{ get_self(), "active"_n },
    "eosio.token"_n,
    "transfer"_n,
    make_tuple( get_self(), client, quantity, msg )
  ).send();

  #ifdef DEBUG_PRINT
    print_f("[InheritAgent::clientclaim] %\n", msg);
  #endif
}

//-----------------------------------------------------------------------------
// ------ notification response
void InheritAgent::ondeposit(const name& from, const name& to, const asset& quantity, const string& memo) {
  // --> Note: deposit/claim (eosio.token transfer) will not record in agent
  // only response when recipient is self and memo message is "miner" or "client"
  if ( to == get_self() ) {
    if ( memo == "miner" ) {
      MinerDataIndex minerData( get_self(), get_self().value );
      auto minerDataItr = minerData.find( from.value );
      if ( minerDataItr == minerData.end() ) {
        minerData.emplace( get_self(), [&](auto& row) {
          row.miner = from;
          row.deposit = quantity;
          row.fee = quantity - quantity;
          row.reward = row.fee;
          row.tryCount = 0;
          row.lastTryTime = 0;
          row.lastClaimTime = 0;
        });
      }
      else {
        minerData.modify( minerDataItr, get_self(), [&](auto& row) {
          row.deposit += quantity;
        });
      }
      #ifdef DEBUG_PRINT
        print_f("[InheritAgent::ondeposit] receive miner deposit from %, quantity: %, memo: %\n",
                 from, quantity, memo);
      #endif
    }
    else if ( memo == "client" ) {
      ClientDataIndex clientData( get_self(), get_self().value );
      auto clientDataItr = clientData.find( from.value );
      if ( clientDataItr == clientData.end() ) {
        clientData.emplace( get_self(), [&](auto& row) {
          row.client = from;
          row.deposit = quantity;
          row.fee = quantity - quantity;
          row.refund = quantity;
        });
      }
      else {
        clientData.modify( clientDataItr, get_self(), [&](auto& row) {
          row.deposit += quantity;
          row.refund += quantity;
        });
      }
      #ifdef DEBUG_PRINT
        print_f("[InheritAgent::ondeposit] receive client deposit from %, quantity: %, memo: %\n",
                 from, quantity, memo);
      #endif
    }
    else {  // return to sender
      action(
        permission_level{ get_self(), "active"_n },
        "eosio.token"_n,
        "transfer"_n,
        make_tuple( get_self(), from, quantity, string("only accept memo: 'miner' or 'client'") )
      ).send();
    }
  } // end of if ( to == get_self() )
}

//-----------------------------------------------------------------------------
// ------ below define the action/function for debug only purpose
#ifdef DEBUG

ACTION InheritAgent::cleardata() {
  // check auth, args
  require_auth( get_self() );

  MinerDataIndex minerData( get_self(), get_self().value );
  auto minerDataItr = minerData.begin();
  while ( minerDataItr != minerData.end() ) {
    minerDataItr = minerData.erase( minerDataItr );
  }

  MinerBillIndex minerBill( get_self(), get_self().value );
  auto minerBillItr = minerBill.begin();
  while ( minerBillItr != minerBill.end() ) {
    minerBillItr = minerBill.erase( minerBillItr );
  }

  ClientDataIndex clientData( get_self(), get_self().value );
  auto clientDataItr = clientData.begin();
  while ( clientDataItr != clientData.end() ) {
    clientDataItr = clientData.erase( clientDataItr );
  }

  ClientBillIndex clientBill( get_self(), get_self().value );
  auto clientBillItr = clientBill.begin();
  while ( clientBillItr != clientBill.end() ) {
    clientBillItr = clientBill.erase( clientBillItr );
  }

  #ifdef DEBUG_PRINT
    print_f("[InheritAgent::cleardata] clear data table");
  #endif
}

ACTION InheritAgent::printtime() {
  // check auth, args
  #ifdef DEBUG_PRINT
    print_f("[InheritAgent::printtime] time: %\n", _timenow());
  #endif
}
#endif // DEBUG
