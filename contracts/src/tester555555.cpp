#include <eosio/eosio.hpp>
#include <eosio/system.hpp>
#include <eosio/asset.hpp>

using namespace std;
using namespace eosio;

static constexpr name ORACLE_CONTRACT = name("rng.oracle");
static constexpr uint32_t MONTH_SECONDS = 2629743;
static constexpr uint32_t HOUR_SECONDS = 3600;
static constexpr uint32_t MINUTE_SECONDS= 60;
static constexpr int64_t DIVISOR = 10000;
static constexpr uint64_t SEED = 7654325491;
static constexpr uint64_t REQUEST_ID = 999;
static constexpr int EIGHT = 8;

class [[eosio::contract("tester555555")]] tester555555 : public contract {  
   
    public:    
        using contract::contract;    
        tester555555(name receiver, name code, datastream<const char *> ds) : contract(receiver, code, ds),token_symbol("TLOS", 4){};
        ~tester555555() {};   

        [[eosio::action]]    
        void startlotto(){      
            require_auth(get_self());   

            lottery_table lotteries(get_self(), get_self().value);    
            auto itr = lotteries.find(get_self().value);

            if(itr==lotteries.end()){
                lotteries.emplace(get_self(), [&]( auto &e) {
                    e.id = get_self();
                    e.end_lottery = new_end();
                    e.max = 0;
                    e.is_open = true;    
                });
            }else{
                lotteries.modify(itr,get_self(), [&]( auto &e) {
                    e.end_lottery = new_end();
                    e.max = 0;
                    e.is_open = true;
                });
            };  
        };

        [[eosio::action]]    
        void endlotto(){      
            
            lottery_table lotteries(get_self(), get_self().value);    
            auto itr = lotteries.find(get_self().value);

            int32_t current_time = now();
            check( current_time > itr->end_lottery,"lottery still active" );

            if (itr->max == 0){
                lotteries.modify(itr,get_self(), [&]( auto &e) {
                    e.end_lottery = new_end();
                    e.is_open = true;
                });
                return;
            };

            lotteries.modify(itr,get_self(), [&]( auto &e) {
                e.is_open = false;
            });

            action(
                {get_self(), "active"_n}, 
                ORACLE_CONTRACT, "requestrand"_n,
                tuple(REQUEST_ID, SEED, get_self())
            ).send();  
        };


        [[eosio::on_notify("eosio.token::transfer")]]    
        void deposit(name player, name to, asset quantity, string memo) {   
            require_auth(player); 

            if (player == get_self() || to != get_self()){        
                return;      
            }  

            lottery_table lotteries(get_self(), get_self().value);    
            auto itr = lotteries.find(get_self().value);  

            int32_t current_time = now();

            check(current_time < itr->end_lottery,"current lottery is closed, try again" );
            check(itr->is_open,"current lottery is closed, try again");
            check(quantity.amount >= 1.0, "in order to participate you must send at least 1.0000 TLOS");      
            check(quantity.amount == (int)quantity.amount, "cannot send fractional amounts");    
            check(quantity.symbol == token_symbol, "only accepting TLOS at this time");  

            balance_table balance(get_self(), get_self().value);      
            auto token_row = balance.find(token_symbol.raw());      
            
            if (token_row != balance.end()) { 
                balance.modify(token_row, get_self(), [&](auto &row) {          
                    row.funds += quantity;        
                });      
            } else {       
                balance.emplace(get_self(), [&](auto &row) {          
                    row.funds = quantity;        
                });
            } 
            
            int64_t ticketCount = quantity.amount / DIVISOR;
            addentry(player, ticketCount); 
        };    

        [[eosio::action]]    
        void receiverand(uint64_t caller_id, checksum256 random) {
            require_auth(ORACLE_CONTRACT);

            auto byte_array = random.extract_as_byte_array();
            uint64_t random_int = 0;

            for (int i = 0; i < EIGHT; i++) {
                random_int <<= EIGHT;
                random_int |= (uint64_t)byte_array[i];
            };

            lottery_table lotteries(get_self(), get_self().value);    
            auto itr = lotteries.find(get_self().value);

            uint64_t min = 0;
            uint64_t max = itr->max;
            uint64_t number = random_int % ++max;

            auto winner = get_winner(number);

            balance_table balance(get_self(), get_self().value);      
            auto token_row = balance.find(token_symbol.raw()); 

            action{        
                permission_level{get_self(), "active"_n},        
                "eosio.token"_n,        
                "transfer"_n,        
                make_tuple(get_self(), winner, token_row->funds, string("test"))      
            }.send();

            clear_table();   

            balance.modify(token_row, get_self(), [&](auto &row) {          
                    row.funds.amount = 0;        
            });  

            action(
                {get_self(), "active"_n}, 
                get_self(), "startlotto"_n,
                ""
            ).send();  
        };
    
    private: 

        const symbol token_symbol; 

        struct [[eosio::table]] balance {      
            eosio::asset funds;      
            uint64_t primary_key() const { return funds.symbol.raw();}   
            
            EOSLIB_SERIALIZE(balance,(funds))
        };    

        using balance_table = multi_index<"balance"_n, balance>;  

        struct [[eosio::table]] entry {
            name id;
            int64_t tickets;
            uint64_t primary_key() const { return id.value; }

            EOSLIB_SERIALIZE(entry,(id)(tickets))
        };

        using entries_table = multi_index<"entries"_n, entry>;

        struct [[eosio::table]] lottery {
            name id;
            uint64_t max;
            uint32_t end_lottery;
            bool is_open;
            uint64_t primary_key() const { return id.value; }

            EOSLIB_SERIALIZE(lottery, (id)(max)(end_lottery)(is_open))
        };

        using lottery_table = multi_index<name("lotteries"), lottery>; 

        void addentry( name &id, int64_t &tickets ) {

            entries_table entries(get_self(), get_self().value);
            auto itr = entries.find(id.value);

            if(itr==entries.end()){
                entries.emplace(get_self(), [&]( auto &e) {
                    e.id = id;
                    e.tickets = tickets;
                });
            }else{
                entries.modify(itr,get_self(), [&]( auto &e) {
                    e.tickets += tickets;
                });
            };

            lottery_table lotteries(get_self(), get_self().value);    
            auto itrl = lotteries.find(get_self().value);

            if(itrl==lotteries.end()){
                lotteries.emplace(get_self(), [&]( auto &f) {
                    f.id = get_self();
                    f.max = tickets;
                });
            }else{
                lotteries.modify(itrl,get_self(), [&]( auto &f) {
                    f.max += tickets;
                });
            };  
        };

        void clear_table() {
            entries_table entries(get_self(), get_self().value);
            auto itr = entries.begin();
            while (itr != entries.end()) {
                itr = entries.erase(itr);
            };
        };

        name get_winner(int64_t random) {
            vector<int64_t> entry_arr; 

            entries_table entries(get_self(), get_self().value);
            auto itr = entries.begin();

            while (itr != entries.end()) {
                for (int64_t i=0; i< itr->tickets; i++){
                    entry_arr.push_back(itr->id.value);
                }
                itr++;
            };

            auto winner_value = entry_arr[random]; 
            auto winner = entries.find(winner_value);
            return winner->id;
        };

        uint32_t new_end() { 
            uint32_t current =  current_time_point().sec_since_epoch();
            current += MINUTE_SECONDS;
            return current;
        }

        uint32_t now() { return current_time_point().sec_since_epoch();}
};


