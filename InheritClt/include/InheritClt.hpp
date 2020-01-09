#include <eosio/eosio.hpp>
#include <eosio/asset.hpp>
#include <eosio/system.hpp>

using namespace eosio;
using namespace std;

CONTRACT InheritClt : public contract {
  public:
    using contract::contract;
    InheritClt(name receiver, name code, datastream<const char*> ds)
    : contract(receiver, code, ds)
    {}

    // --- actions
    ACTION init();

    ACTION allocate(const name& inheritor, const name& tokencontract, const asset& quantity,
                    uint32_t validFrom, uint32_t cdDuration, const string& remark);

    ACTION unallocate(const name& inheritor, const name& tokencontract, const symbol& sym);

    ACTION freeze(const name& inheritor, const name& tokencontract, const symbol& sym);

    ACTION setenable(bool enabled);

    ACTION onagentmine(const name& inheritor, const name& tokencontract, const asset& quantity,
                       const name& assetclient, const name& miner);

    // --- notification response
    // [[eosio::on_notify("inheritagent::mine")]]
    // void onmine(const name& inheritor, const name& tokencontract, const asset& quantity,
    //             const name& assetclient, const name& miner);

#ifdef DEBUG
    ACTION clearinherit(const name& inheritor);
    ACTION clearalloc(const name& tokencontract);
    ACTION cleartrans(const name& tokencontract);
    ACTION printtime();
#endif

  private:  
    // global flag enable or disable the inheritance
    TABLE GlobalFlag {
      uint64_t  key;
      bool      miningEnabled;
      uint64_t  primary_key() const { return key; }
    };
    typedef eosio::multi_index<"globalflag"_n, GlobalFlag> GlobalFlagIndex;

    // inheritance contract record state
    typedef enum {
      FROZEN          = 0,
      ACTIVE          = 1,
      ACTIVECD_MINED  = 2,
      TRANSFER_MINED  = 3
    } EState;
    typedef uint8_t State;

    // --- table of inheritance records
    TABLE Inheritance { // scoped by inheritor
      uint64_t        id;
      State           state;
      extended_asset  willGet;
      uint32_t        validFrom;
      uint32_t        cdBeganTime;
      uint32_t        cdDuration;
      string          remark;
      uint64_t  primary_key() const { return id; }
      uint64_t  get_token_code() const { return willGet.contract.value; }
      uint64_t  get_token_symc() const { return willGet.quantity.symbol.code().raw(); }
      uint128_t get_unique_tkn() const { return ( static_cast<uint128_t>(get_token_code()) << 64 ) | get_token_symc(); }
      uint64_t  get_valid_from() const { return static_cast<uint64_t>(validFrom); }
    };
    typedef eosio::multi_index<
      "inheritance"_n, Inheritance,
      indexed_by<"tokencode"_n, const_mem_fun<Inheritance, uint64_t, &Inheritance::get_token_code>>,
      indexed_by<"tokensymc"_n, const_mem_fun<Inheritance, uint64_t, &Inheritance::get_token_symc>>,
      indexed_by<"uniquetkn"_n, const_mem_fun<Inheritance, uint128_t, &Inheritance::get_unique_tkn>>,
      indexed_by<"validfrom"_n, const_mem_fun<Inheritance, uint64_t, &Inheritance::get_valid_from>>
      > InheritanceIndex;

    // --- table of transfered after mining
    TABLE Transfered { // scoped by token contract
      uint64_t  id;
      name      receiver;
      asset     got;
      uint32_t  validFrom;
      uint32_t  cdBeganTime;
      uint32_t  cdDuration;
      uint32_t  transferedTime;
      string    remark;
      uint64_t  primary_key() const { return id; }
      uint64_t  get_token_symc() const { return got.symbol.code().raw(); }
      uint128_t get_rcvr_token() const { return ( static_cast<uint128_t>(receiver.value) << 64 ) | get_token_symc(); }
      uint64_t  get_valid_from() const { return static_cast<uint64_t>(validFrom); }
    };
    typedef eosio::multi_index<
      "transfered"_n, Transfered,
      indexed_by<"tokensymc"_n, const_mem_fun<Transfered, uint64_t, &Transfered::get_token_symc>>,
      indexed_by<"rcvrtoken"_n, const_mem_fun<Transfered, uint128_t, &Transfered::get_rcvr_token>>,
      indexed_by<"validfrom"_n, const_mem_fun<Transfered, uint64_t, &Transfered::get_valid_from>>
      > TransferedIndex;

    // --- table of self's asset allocated and unallocated
    TABLE Allocation {  // scoped by contract
      asset allocated;
      asset unallocated;
      asset transfered;
      uint64_t primary_key() const { return unallocated.symbol.code().raw(); }
    };
    typedef eosio::multi_index<"allocation"_n, Allocation> AllocationIndex;

    // --- for indexing external table in eosio.token or eosio.token-like contract
    struct Account {  // same as the struct in eosio.token
      asset balance;
      uint64_t primary_key() const { return balance.symbol.code().raw(); }
    };
    typedef eosio::multi_index<"accounts"_n, Account> AccountIndex;

    // --- helper methods
    bool _miningEnabled() const;
    uint32_t _timenow() const { return current_time_point().sec_since_epoch(); }
};
