#include <eosio/eosio.hpp>
#include <eosio/asset.hpp>
#include <eosio/system.hpp>

using namespace eosio;
using namespace std;

CONTRACT InheritAgent : public contract {
  public:
    using contract::contract;

    // --- actions
    ACTION init();

    ACTION selfclaim(const name& to);

    ACTION clientclaim(const name& client);

    ACTION minerclaim(const name& miner);

    ACTION mine(const name& inheritor, const name& tokencontract, const asset& quantity,
                const name& assetclient, const name& miner);

    // --- notification response
    [[eosio::on_notify("eosio.token::transfer")]]
    void ondeposit(const name& from, const name& to, const asset& quantity, const string& memo);

    [[eosio::on_notify("*::onagentmine")]]
    void didmine(const name& inheritor, const name& tokencontract, const asset& quantity,
                 const name& assetclient, const name& miner);

#ifdef DEBUG
    ACTION cleardata();
    ACTION printtime();
#endif

  private:
    // self variables
    TABLE SelfVar {
      uint64_t  key;
      bool      enabled;
      asset     earnings;
      uint64_t  primary_key() const { return key; }
    };
    typedef eosio::multi_index<"selfvar"_n, SelfVar> SelfVarIndex;

    // --- miner data
    TABLE MinerData {  // scoped by self
      name      miner;
      asset     deposit;
      asset     fee;
      asset     reward;
      uint8_t   tryCount;
      uint32_t  lastTryTime;
      uint32_t  lastClaimTime;
      uint64_t  primary_key() const { return miner.value; }
    };
    typedef eosio::multi_index<"minerdata"_n, MinerData> MinerDataIndex;

    // --- miner bill
    TABLE MinerBill {  // scoped by self
      uint64_t  id;
      name      payer;
      name      payee;
      asset     quantity;
      uint8_t   type;
      uint32_t  date;
      uint64_t  primary_key() const { return id; }
    };
    typedef eosio::multi_index<"minerbill"_n, MinerBill> MinerBillIndex;

    // --- client data summary
    TABLE ClientData {  // scoped by self
      name      client;
      asset     deposit;
      asset     fee;
      asset     refund;
      uint32_t  lastClaimTime;
      uint64_t  primary_key() const { return client.value; }
    };
    typedef eosio::multi_index<"clientdata"_n, ClientData> ClientDataIndex;

    // --- client bill
    TABLE ClientBill {  // scoped by self
      uint64_t  id;
      name      payer;
      name      payee;
      asset     quantity;
      uint8_t   type;
      uint32_t  date;
      uint64_t  primary_key() const { return id; }
    };
    typedef eosio::multi_index<"clientbill"_n, ClientBill> ClientBillIndex;

    // --- indexing external table of inheritance records
    typedef uint8_t State;
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

    // --- helper methods
    uint32_t _timenow() const { return current_time_point().sec_since_epoch(); }
    void _earn(const asset& quantity);
};
