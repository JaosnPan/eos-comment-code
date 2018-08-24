/**
 *  @file
 *  @copyright defined in eos/LICENSE.txt
 */
#include "eosio.system.hpp"

#include <eosiolib/eosio.hpp>
#include <eosiolib/print.hpp>
#include <eosiolib/datastream.hpp>
#include <eosiolib/serialize.hpp>
#include <eosiolib/multi_index.hpp>
#include <eosiolib/privileged.h>
#include <eosiolib/transaction.hpp>

#include <eosio.token/eosio.token.hpp>


#include <cmath>
#include <map>

namespace eosiosystem {
   using eosio::asset;
   using eosio::indexed_by;
   using eosio::const_mem_fun;
   using eosio::bytes;
   using eosio::print;
   using eosio::permission_level;
   using std::map;
   using std::pair;

   static constexpr time refund_delay = 3*24*3600;
   static constexpr time refund_expiration_time = 3600;

   struct user_resources {
      account_name  owner;
      asset         net_weight;
      asset         cpu_weight;
      int64_t       ram_bytes = 0;

      uint64_t primary_key()const { return owner; }

      // explicit serialization macro is not necessary, used here only to improve compilation time
      EOSLIB_SERIALIZE( user_resources, (owner)(net_weight)(cpu_weight)(ram_bytes) )
   };


   /**
    *  Every user 'from' has a scope/table that uses every receipient 'to' as the primary key.
    */
   struct delegated_bandwidth {
      account_name  from;
      account_name  to;
      asset         net_weight;
      asset         cpu_weight;

      uint64_t  primary_key()const { return to; }

      // explicit serialization macro is not necessary, used here only to improve compilation time
      EOSLIB_SERIALIZE( delegated_bandwidth, (from)(to)(net_weight)(cpu_weight) )

   };

   struct refund_request {
      account_name  owner;
      time          request_time;
      eosio::asset  net_amount;
      eosio::asset  cpu_amount;

      uint64_t  primary_key()const { return owner; }

      // explicit serialization macro is not necessary, used here only to improve compilation time
      EOSLIB_SERIALIZE( refund_request, (owner)(request_time)(net_amount)(cpu_amount) )
   };

   /**
    *  These tables are designed to be constructed in the scope of the relevant user, this
    *  facilitates simpler API for per-user queries[这些表旨在构建在相关用户的范围内为每个用户的查询提供更简单的API]
    */
   typedef eosio::multi_index< N(userres), user_resources>      user_resources_table;
   typedef eosio::multi_index< N(delband), delegated_bandwidth> del_bandwidth_table;
   typedef eosio::multi_index< N(refunds), refund_request>      refunds_table;



   /**
    *  This action will buy an exact amount of ram and bill the payer the current market price.
    */
   void system_contract::buyrambytes( account_name payer, account_name receiver, uint32_t bytes ) {
      auto itr = _rammarket.find(S(4,RAMCORE));
      auto tmp = *itr;
      auto eosout = tmp.convert( asset(bytes,S(0,RAM)), CORE_SYMBOL );

      buyram( payer, receiver, eosout );
   }


   /**
    *  When buying ram the payer irreversiblly transfers quant to system contract and only
    *  the receiver may reclaim the tokens via the sellram action[当购买ram时，付款人不可逆转地将数量转移到系统合同而且只是接收者可以通过sellram动作回收代币]. 
    *  The receiver pays for the storage of all database records associated with this action[接收者为此付费存储与此操作关联的所有数据库记录].
    *
    *  RAM is a scarce resource whose supply is defined by global properties max_ram_size[RAM是一种稀缺资源，其供应由全局属性max_ram_size定义]. RAM is
    *  priced using the bancor algorithm such that price-per-byte with a constant reserve ratio of 100:1[RAM是使用bancor算法定价，使得每字节价格具有100：1的恒定储备比率].
    *  
    *  payer:     购买者账户
    *  receiver:  接收者账户
    *  quant:     用于购买的财富
    */
   void system_contract::buyram( account_name payer, account_name receiver, asset quant )
   {
      /** 验证购买者权限并且提供的财富结构体中必须有余额 */
      require_auth( payer );
      eosio_assert( quant.amount > 0, "must purchase a positive amount" );

      /** RAM的手续费，参与计算的EOS数量+199除以200， 即%5的手续费 */
      auto fee = quant;
      fee.amount = ( fee.amount + 199 ) / 200; /// .5% fee (round up)
      // fee.amount cannot be 0 since that is only possible if quant.amount is 0 which is not allowed by the assert above.
      // If quant.amount == 1, then fee.amount == 1,
      // otherwise if quant.amount > 1, then 0 < fee.amount < quant.amount.
      // [fee.amount不能为0，因为只有当quant.amount为0且上述断言不允许时才可以
      // 如果quant.amount == 1，那么fee.amount == 1，否则如果quant.amount> 1，则0 <fee.amount <quant.amount]
      /**
       * quant_after_fee为买完内存后的余额
      */
      auto quant_after_fee = quant;
      quant_after_fee.amount -= fee.amount;
      // quant_after_fee.amount should be > 0 if quant.amount > 1.
      // If quant.amount == 1, then quant_after_fee.amount == 0 and the next inline transfer will fail causing the buyram action to fail.
      // [如果quant.amount> 1，则quant_after_fee.amount应> 0。如果quant.amount == 1，则quant_after_fee.amount == 0并且下一个内联传输将失败，导致buyram操作失败]

      /** 发起一笔内联转账操作，调用token合约，将扣除5%的余额转给eosio.ram账户，设置附言为buy ram */
      INLINE_ACTION_SENDER(eosio::token, transfer)( N(eosio.token), {payer,N(active)},
         { payer, N(eosio.ram), quant_after_fee, std::string("buy ram") } );

      /** 发起一笔内联转账操作，调用token合约，将5%的手续费转给eosio.ramfee账户，设置附言为ram fee */
      if( fee.amount > 0 ) {
         INLINE_ACTION_SENDER(eosio::token, transfer)( N(eosio.token), {payer,N(active)},
                                                       { payer, N(eosio.ramfee), fee, std::string("ram fee") } );
      }

      int64_t bytes_out;
      /** 根据_rammarket市场里的EOS和RAM实时的计算出能够购买的RAM量并修改内存市场中的值 */
      const auto& market = _rammarket.get(S(4,RAMCORE), "ram market does not exist");
      _rammarket.modify( market, 0, [&]( auto& es ) {
          bytes_out = es.convert( quant_after_fee,  S(0,RAM) ).amount;
      });
      /** 要购买的RAM必须是一个正数 */
      eosio_assert( bytes_out > 0, "must reserve a positive amount" );

      /** 更新全网已购买的内存值total_ram_bytes_reserved和已经用于购买内存抵押的EOS总量total_ram_stake(这个总量是扣除手续费后的总量) */
      _gstate.total_ram_bytes_reserved += uint64_t(bytes_out);
      _gstate.total_ram_stake          += quant_after_fee.amount;

      /**
      *  获取接受者的usertable项(智能合约里对table的修改其实会回调到eos的db相关函数)
      *  如果接收者在,查找该资源中是否存在接收者
      *  如果存在则直接将该对象的ram资源+上本次购买的资源
      *  如果不存在则直接将该对象是的内存余额设置为本次购买的余额
      */
      user_resources_table  userres( _self, receiver );
      auto res_itr = userres.find( receiver );
      if( res_itr ==  userres.end() ) {
         /** 增加一个对象，会调用db_store_i64函数 */
         res_itr = userres.emplace( receiver, [&]( auto& res ) {
               res.owner = receiver;
               res.ram_bytes = bytes_out;
            });
      } else {
         userres.modify( res_itr, receiver, [&]( auto& res ) {
               res.ram_bytes += bytes_out;
            });
      }
      /** 调用set_resource_limits函数设置接收者的资源余量 */
      set_resource_limits( res_itr->owner, res_itr->ram_bytes, res_itr->net_weight.amount, res_itr->cpu_weight.amount );
   }


   /**
    *  The system contract now buys and sells RAM allocations at prevailing market prices.
    *  This may result in traders buying RAM today in anticipation of potential shortages
    *  tomorrow. Overall this will result in the market balancing the supply and demand
    *  for RAM over time.
    */
   void system_contract::sellram( account_name account, int64_t bytes ) {
      require_auth( account );
      eosio_assert( bytes > 0, "cannot sell negative byte" );

      user_resources_table  userres( _self, account );
      auto res_itr = userres.find( account );
      eosio_assert( res_itr != userres.end(), "no resource row" );
      eosio_assert( res_itr->ram_bytes >= bytes, "insufficient quota" );

      asset tokens_out;
      auto itr = _rammarket.find(S(4,RAMCORE));
      _rammarket.modify( itr, 0, [&]( auto& es ) {
          /// the cast to int64_t of bytes is safe because we certify bytes is <= quota which is limited by prior purchases
          tokens_out = es.convert( asset(bytes,S(0,RAM)), CORE_SYMBOL);
      });

      eosio_assert( tokens_out.amount > 1, "token amount received from selling ram is too low" );

      _gstate.total_ram_bytes_reserved -= static_cast<decltype(_gstate.total_ram_bytes_reserved)>(bytes); // bytes > 0 is asserted above
      _gstate.total_ram_stake          -= tokens_out.amount;

      //// this shouldn't happen, but just in case it does we should prevent it
      eosio_assert( _gstate.total_ram_stake >= 0, "error, attempt to unstake more tokens than previously staked" );

      userres.modify( res_itr, account, [&]( auto& res ) {
          res.ram_bytes -= bytes;
      });
      set_resource_limits( res_itr->owner, res_itr->ram_bytes, res_itr->net_weight.amount, res_itr->cpu_weight.amount );

      INLINE_ACTION_SENDER(eosio::token, transfer)( N(eosio.token), {N(eosio.ram),N(active)},
                                                       { N(eosio.ram), account, asset(tokens_out), std::string("sell ram") } );

      auto fee = ( tokens_out.amount + 199 ) / 200; /// .5% fee (round up)
      // since tokens_out.amount was asserted to be at least 2 earlier, fee.amount < tokens_out.amount
      
      if( fee > 0 ) {
         INLINE_ACTION_SENDER(eosio::token, transfer)( N(eosio.token), {account,N(active)},
            { account, N(eosio.ramfee), asset(fee), std::string("sell ram fee") } );
      }
   }

   /** 验证B1的归属 */
   void validate_b1_vesting( int64_t stake ) {
      const int64_t base_time = 1527811200; /// 2018-06-01
      const int64_t max_claimable = 100'000'000'0000ll;
      const int64_t claimable = int64_t(max_claimable * double(now()-base_time) / (10*seconds_per_year) );

      eosio_assert( max_claimable - claimable <= stake, "b1 can only claim their tokens over 10 years" );
   }
   
   /**
     *  修改抵押信息
     *  from:              抵押者账户
     *  receiver:          接收者账户
     *  stake_net_delta:   用于修改网络的财富（可以使负数，因为解除抵押也使用这个接口）
     *  stake_cpu_delta:   用于修改CPU的财富（可以使负数，因为解除抵押也使用这个接口）
     *  transfer：          是否转移
   */
   void system_contract::changebw( account_name from, account_name receiver,
                                   const asset stake_net_delta, const asset stake_cpu_delta, bool transfer )
   {
      /** 验证抵押者账户，网络+CPU的总抵押量的绝对值要大于单类资源的绝对值之和 */
      require_auth( from );
      eosio_assert( stake_net_delta != asset(0) || stake_cpu_delta != asset(0), "should stake non-zero amount" );
      eosio_assert( std::abs( (stake_net_delta + stake_cpu_delta).amount )
                     >= std::max( std::abs( stake_net_delta.amount ), std::abs( stake_cpu_delta.amount ) ),
                    "net and cpu deltas cannot be opposite signs" );

      account_name source_stake_from = from;
      if ( transfer ) {
         from = receiver;
      }

      // update stake delegated from "from" to "receiver"[更新从“from”到“receiver”的委托]
      {
         /** 
           *   在委托者的资源抵押委托表中查找接受者，
           *   如果未找到则增加一项，并设置资源的权重，否则修改资源的权重 
          */
         del_bandwidth_table     del_tbl( _self, from);
         auto itr = del_tbl.find( receiver );
         if( itr == del_tbl.end() ) {
            itr = del_tbl.emplace( from, [&]( auto& dbo ){
                  dbo.from          = from;
                  dbo.to            = receiver;
                  dbo.net_weight    = stake_net_delta;
                  dbo.cpu_weight    = stake_cpu_delta;
               });
         }
         else {
            del_tbl.modify( itr, 0, [&]( auto& dbo ){
                  dbo.net_weight    += stake_net_delta;
                  dbo.cpu_weight    += stake_cpu_delta;
               });
         }
         /** 目前资源的抵押不能为负，即解除抵押不能比原有抵押的值大 */
         eosio_assert( asset(0) <= itr->net_weight, "insufficient staked net bandwidth" );
         eosio_assert( asset(0) <= itr->cpu_weight, "insufficient staked cpu bandwidth" );
         /** 如果抵押获得的两类资源都为0，则删除抵押委托表中的这一项 */
         if ( itr->net_weight == asset(0) && itr->cpu_weight == asset(0) ) {
            del_tbl.erase( itr );
         }
      } // itr can be invalid, should go out of scope

      // update totals of "receiver"[更新接收者资源表]
      {
         /**
           *   查找接收者的资源表
           *   如果接收者存在，则修改接收者资源
           *   否则增加一项，修改资源所属和资源量
         */
         user_resources_table   totals_tbl( _self, receiver );
         auto tot_itr = totals_tbl.find( receiver );
         if( tot_itr ==  totals_tbl.end() ) {
            tot_itr = totals_tbl.emplace( from, [&]( auto& tot ) {
                  tot.owner = receiver;
                  tot.net_weight    = stake_net_delta;
                  tot.cpu_weight    = stake_cpu_delta;
               });
         } else {
            totals_tbl.modify( tot_itr, from == receiver ? from : 0, [&]( auto& tot ) {
                  tot.net_weight    += stake_net_delta;
                  tot.cpu_weight    += stake_cpu_delta;
               });
         }
         /** 资源的拥有量不能为负，即解除抵押后资源不能比原有抵押的值大 */
         eosio_assert( asset(0) <= tot_itr->net_weight, "insufficient staked total net bandwidth" );
         eosio_assert( asset(0) <= tot_itr->cpu_weight, "insufficient staked total cpu bandwidth" );
         /** 设置接收者资源的限制 */
         set_resource_limits( receiver, tot_itr->ram_bytes, tot_itr->net_weight.amount, tot_itr->cpu_weight.amount );
         /** 如果三类资源都为0，在资源表中删除接收者拥有的这项 */
         if ( tot_itr->net_weight == asset(0) && tot_itr->cpu_weight == asset(0)  && tot_itr->ram_bytes == 0 ) {
            totals_tbl.erase( tot_itr );
         }
      } // tot_itr can be invalid, should go out of scope

      // create refund or update from existing refund[从现有退款中创建退款或更新]
      if ( N(eosio.stake) != source_stake_from ) { //for eosio both transfer and refund make no sense[对于eosio，转移和退款都没有意义]
         /** 在退款表中查找资源抵押者 */
         refunds_table refunds_tbl( _self, from );
         auto req = refunds_tbl.find( from );

         //create/update/delete refund
         auto net_balance = stake_net_delta;
         auto cpu_balance = stake_cpu_delta;
         bool need_deferred_trx = false;


         // net and cpu are same sign by assertions in delegatebw and undelegatebw
         // redundant assertion also at start of changebw to protect against misuse of changebw[net和cpu是delegatebw和undelegatebw中的断言相同的符号
		 // 也是在changebw开始时的冗余断言，以防止滥用changebw]
         bool is_undelegating = (net_balance.amount + cpu_balance.amount ) < 0;
         bool is_delegating_to_self = (!transfer && from == receiver);
         /** 如果是委托给自己并且是解除抵押 */
         if( is_delegating_to_self || is_undelegating ) {
            if ( req != refunds_tbl.end() ) { //need to update refund[需要更新抵押表]
               refunds_tbl.modify( req, 0, [&]( refund_request& r ) {
                  /** 
                    *   如果net和cpu资源解除抵押（有一个为负数），记录请求时间为当前时间
                    *   如果在资源抵押表中net或者cpu资源减去本次抵押量小于0，意味着是解除抵押，那么修改表中的数据为0
                    *   如果大于0，则设置局部变量为0
                   */
                  if ( net_balance < asset(0) || cpu_balance < asset(0) ) {
                     r.request_time = now();
                  }
                  r.net_amount -= net_balance;
                  if ( r.net_amount < asset(0) ) {
                     net_balance = -r.net_amount;
                     r.net_amount = asset(0);
                  } else {
                     net_balance = asset(0);
                  }
                  r.cpu_amount -= cpu_balance;
                  if ( r.cpu_amount < asset(0) ){
                     cpu_balance = -r.cpu_amount;
                     r.cpu_amount = asset(0);
                  } else {
                     cpu_balance = asset(0);
                  }
               });
               /** 资源不可能为0 */
               eosio_assert( asset(0) <= req->net_amount, "negative net refund amount" ); //should never happen
               eosio_assert( asset(0) <= req->cpu_amount, "negative cpu refund amount" ); //should never happen
               /** 如果表中的资源余额都为0，则在资源表中删除这一些，并设置延迟交易为ture，否则设置延迟交易为false */
               if ( req->net_amount == asset(0) && req->cpu_amount == asset(0) ) {
                  refunds_tbl.erase( req );
                  need_deferred_trx = false;
               } else {
                  need_deferred_trx = true;
               }

            } else if ( net_balance < asset(0) || cpu_balance < asset(0) ) { //need to create refund[需要创建退款]
               /** 
                 *   在解除抵押的操作中，如果局部变量为负数，即需要创建退款
                 *   设置完退款后设置延迟交易为ture并设置请求时间
                */
               refunds_tbl.emplace( from, [&]( refund_request& r ) {
                  r.owner = from;
                  if ( net_balance < asset(0) ) {
                     r.net_amount = -net_balance;
                     net_balance = asset(0);
                  } // else r.net_amount = 0 by default constructor
                  if ( cpu_balance < asset(0) ) {
                     r.cpu_amount = -cpu_balance;
                     cpu_balance = asset(0);
                  } // else r.cpu_amount = 0 by default constructor
                  r.request_time = now();
               });
               need_deferred_trx = true;
            } // else stake increase requested with no existing row in refunds_tbl -> nothing to do with refunds_tbl[在refunds_tbl中没有现有行请求其他权益增加 - >与refunds_tbl无关]
         } /// end if is_delegating_to_self || is_undelegating

         /** 如果是延迟交易，则创建一个refund_delay为3天的退款交易请求并取消原有的延迟交易请求并发送该请求，否则直接取消延迟交易请求 */
         if ( need_deferred_trx ) {
            eosio::transaction out;
            out.actions.emplace_back( permission_level{ from, N(active) }, _self, N(refund), from );
            out.delay_sec = refund_delay;
            cancel_deferred( from ); // TODO: Remove this line when replacing deferred trxs is fixed[修复延迟trxs时删除此行]
            out.send( from, from, true );
         } else {
            cancel_deferred( from );
         }
         /** 如果是抵押资源，即net+cpu >0, 则从抵押者账户中给eosio.stake账户转账，并设置附言为stake bandwidth */
         auto transfer_amount = net_balance + cpu_balance;
         if ( asset(0) < transfer_amount ) {
            INLINE_ACTION_SENDER(eosio::token, transfer)( N(eosio.token), {source_stake_from, N(active)},
               { source_stake_from, N(eosio.stake), asset(transfer_amount), std::string("stake bandwidth") } );
         }
      }

      // update voting power[更新投票权]
      {
         /** 查找投票者表，查找是否存在资源抵押获得者，存在则更新抵押资源的余额，否则增加一个投票者项 */
         asset total_update = stake_net_delta + stake_cpu_delta;
         auto from_voter = _voters.find(from);
         if( from_voter == _voters.end() ) {
            from_voter = _voters.emplace( from, [&]( auto& v ) {
                  v.owner  = from;
                  v.staked = total_update.amount;
               });
         } else {
            _voters.modify( from_voter, 0, [&]( auto& v ) {
                  v.staked += total_update.amount;
               });
         }
         eosio_assert( 0 <= from_voter->staked, "stake for voting cannot be negative");

         /** 如果资源拥有者叫b1，则验证b1的资源归属，从函数内看是b1只能在10年内赎回token */
         if( from == N(b1) ) {
            validate_b1_vesting( from_voter->staked );
         }
         /** 更新投票 */
         if( from_voter->producers.size() || from_voter->proxy ) {
            update_votes( from, from_voter->proxy, from_voter->producers, false );
         }
      }
   }
                                   
  /**
    *  抵押获取资源
    *  from:      抵押者账户
    *  receiver:  接收者账户
    *  stake_net_quantity:     用于购买网络的财富
    *  stake_cpu_quantity:     用于购买CPU的财富
    *  transfer：               是否转移
   */
   void system_contract::delegatebw( account_name from, account_name receiver,
                                     asset stake_net_quantity,
                                     asset stake_cpu_quantity, bool transfer )
   {
      /** 资源抵押可以有一种为0，并且如果转移标记有效时不能委托给自己 */
      eosio_assert( stake_cpu_quantity >= asset(0), "must stake a positive amount" );
      eosio_assert( stake_net_quantity >= asset(0), "must stake a positive amount" );
      eosio_assert( stake_net_quantity + stake_cpu_quantity > asset(0), "must stake a positive amount" );
      eosio_assert( !transfer || from != receiver, "cannot use transfer flag if delegating to self" );

      /** 修改抵押信息 */
      changebw( from, receiver, stake_net_quantity, stake_cpu_quantity, transfer);
   } // delegatebw

   void system_contract::undelegatebw( account_name from, account_name receiver,
                                       asset unstake_net_quantity, asset unstake_cpu_quantity )
   {
      eosio_assert( asset() <= unstake_cpu_quantity, "must unstake a positive amount" );
      eosio_assert( asset() <= unstake_net_quantity, "must unstake a positive amount" );
      eosio_assert( asset() < unstake_cpu_quantity + unstake_net_quantity, "must unstake a positive amount" );
      eosio_assert( _gstate.total_activated_stake >= min_activated_stake,
                    "cannot undelegate bandwidth until the chain is activated (at least 15% of all tokens participate in voting)" );

      changebw( from, receiver, -unstake_net_quantity, -unstake_cpu_quantity, false);
   } // undelegatebw


   void system_contract::refund( const account_name owner ) {
      require_auth( owner );

      refunds_table refunds_tbl( _self, owner );
      auto req = refunds_tbl.find( owner );
      eosio_assert( req != refunds_tbl.end(), "refund request not found" );
      eosio_assert( req->request_time + refund_delay <= now(), "refund is not available yet" );
      // Until now() becomes NOW, the fact that now() is the timestamp of the previous block could in theory
      // allow people to get their tokens earlier than the 3 day delay if the unstake happened immediately after many
      // consecutive missed blocks.

      INLINE_ACTION_SENDER(eosio::token, transfer)( N(eosio.token), {N(eosio.stake),N(active)},
                                                    { N(eosio.stake), req->owner, req->net_amount + req->cpu_amount, std::string("unstake") } );

      refunds_tbl.erase( req );
   }


} //namespace eosiosystem
