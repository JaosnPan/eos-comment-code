#include "eosio.system.hpp"

#include <eosio.token/eosio.token.hpp>

namespace eosiosystem {

   const int64_t  min_pervote_daily_pay = 100'0000;         /** 每天 */
   const int64_t  min_activated_stake   = 150'000'000'0000; /** 激活主网最小的投票数，总数的15% */
   const double   continuous_rate       = 0.04879;          // 5% annual rate(年化通胀率/增发率)
   const double   perblock_rate         = 0.0025;           // 0.25% (超级节点打块奖励25%)
   const double   standby_rate          = 0.0075;           // 0.75% (注册矿工奖励)
   const uint32_t blocks_per_year       = 52*7*24*2*3600;   // half seconds per year(1年的打块量，1s打2块，1年52周)
   const uint32_t seconds_per_year      = 52*7*24*3600;     // 一年的秒数
   const uint32_t blocks_per_day        = 2 * 24 * 3600;    // 一天的打块量
   const uint32_t blocks_per_hour       = 2 * 3600;         // 一小时打块量
   const uint64_t useconds_per_day      = 24 * 3600 * uint64_t(1000000); //一天的微秒
   const uint64_t useconds_per_year     = seconds_per_year*1000000ll;    //一年的微秒


   void system_contract::onblock( block_timestamp timestamp, account_name producer ) {
      using namespace eosio;

      require_auth(N(eosio));

      /** until activated stake crosses this threshold no new rewards are paid */
      if( _gstate.total_activated_stake < min_activated_stake )
         return;

      if( _gstate.last_pervote_bucket_fill == 0 )  /// start the presses
         _gstate.last_pervote_bucket_fill = current_time();


      /**
       * At startup the initial producer may not be one that is registered / elected
       * and therefore there may be no producer object for them.
       */
      auto prod = _producers.find(producer);
      if ( prod != _producers.end() ) {
         _gstate.total_unpaid_blocks++;
         _producers.modify( prod, 0, [&](auto& p ) {
               p.unpaid_blocks++;
         });
      }

      /// only update block producers once every minute, block_timestamp is in half seconds[每分钟只更新一次块，块时间戳在半秒内]
      if( timestamp.slot - _gstate.last_producer_schedule_update.slot > 120 ) {
         update_elected_producers( timestamp );

         if( (timestamp.slot - _gstate.last_name_close.slot) > blocks_per_day ) {
            name_bid_table bids(_self,_self);
            auto idx = bids.get_index<N(highbid)>();
            auto highest = idx.begin();
            if( highest != idx.end() &&
                highest->high_bid > 0 &&
                highest->last_bid_time < (current_time() - useconds_per_day) &&
                _gstate.thresh_activated_stake_time > 0 &&
                (current_time() - _gstate.thresh_activated_stake_time) > 14 * useconds_per_day ) {
                   _gstate.last_name_close = timestamp;
                   idx.modify( highest, 0, [&]( auto& b ){
                         b.high_bid = -b.high_bid;
               });
            }
         }
      }
   }

   using namespace eosio;
   /**
    * 生产者获取奖励
    */
   void system_contract::claimrewards( const account_name& owner ) {
      require_auth(owner);

      /** 查找生产者，生产者必须是有效的生产者 */
      const auto& prod = _producers.get( owner );
      eosio_assert( prod.active(), "producer does not have an active key" );
      /** 投票量必须达到最小的投票量 */
      eosio_assert( _gstate.total_activated_stake >= min_activated_stake,
                    "cannot claim rewards until the chain is activated (at least 15% of all tokens participate in voting)" );

      /** 每天只能获取一次奖励 */
      auto ct = current_time();

      eosio_assert( ct - prod.last_claim_time > useconds_per_day, "already claimed rewards within past day" );

      /** 获取支持的系统货币与最后一次投票桶填充时间到本次投票桶填充时间的差 */
      const asset token_supply   = token( N(eosio.token)).get_supply(symbol_type(system_token_symbol).name() );
      const auto usecs_since_last_fill = ct - _gstate.last_pervote_bucket_fill;

      /** 如果填充投票桶的时间差和最后一次填充投票桶的时间都大于0（即主网已经启动并且已经接受多一次增发请求） */
      if( usecs_since_last_fill > 0 && _gstate.last_pervote_bucket_fill > 0 ) {
         /** 
           * 计算增发量
           * 增发量=（（系统货币的量*通胀率）*奖励桶的填充时间差）/ 一年的微妙时间
          */
         auto new_tokens = static_cast<int64_t>( (continuous_rate * double(token_supply.amount) * double(usecs_since_last_fill)) / double(useconds_per_year) );

         auto to_producers       = new_tokens / 5;                   /** 增发量的20%用于奖励生产者 */
         auto to_savings         = new_tokens - to_producers;        /** 增发量的80%转账给saving账户 */
         auto to_per_block_pay   = to_producers / 4;                 /** 奖励生产者的25%用于奖励出块节点 */
         auto to_per_vote_pay    = to_producers - to_per_block_pay;  /** 奖励生产者的75%用于奖励接收投票的节点 */
         
         /** 发起内联转账，将增发token转账到对应的账户 */
         INLINE_ACTION_SENDER(eosio::token, issue)( N(eosio.token), {{N(eosio),N(active)}},
                                                    {N(eosio), asset(new_tokens), std::string("issue tokens for producer pay and savings")} );

         INLINE_ACTION_SENDER(eosio::token, transfer)( N(eosio.token), {N(eosio),N(active)},
                                                       { N(eosio), N(eosio.saving), asset(to_savings), "unallocated inflation" } );

         INLINE_ACTION_SENDER(eosio::token, transfer)( N(eosio.token), {N(eosio),N(active)},
                                                       { N(eosio), N(eosio.bpay), asset(to_per_block_pay), "fund per-block bucket" } );

         INLINE_ACTION_SENDER(eosio::token, transfer)( N(eosio.token), {N(eosio),N(active)},
                                                       { N(eosio), N(eosio.vpay), asset(to_per_vote_pay), "fund per-vote bucket" } );

         /** 修改全局奖励桶，设置全局奖励桶填充时间为现在 */
         _gstate.pervote_bucket  += to_per_vote_pay;
         _gstate.perblock_bucket += to_per_block_pay;

         _gstate.last_pervote_bucket_fill = ct;
      }

      /** 计算节点可获取的接收投票奖励以及出块奖励，并修改对应的全局参数 */
      int64_t producer_per_block_pay = 0;
      if( _gstate.total_unpaid_blocks > 0 ) {
         producer_per_block_pay = (_gstate.perblock_bucket * prod.unpaid_blocks) / _gstate.total_unpaid_blocks;
      }
      int64_t producer_per_vote_pay = 0;
      if( _gstate.total_producer_vote_weight > 0 ) {
         producer_per_vote_pay  = int64_t((_gstate.pervote_bucket * prod.total_votes ) / _gstate.total_producer_vote_weight);
      }
      /** 如果本次获取接收投票奖励个数小于每天最小的获取数，则只能获取0个奖励 */
      if( producer_per_vote_pay < min_pervote_daily_pay ) {
         producer_per_vote_pay = 0;
      }
      /** 修改全局奖励桶的内容 */
      _gstate.pervote_bucket      -= producer_per_vote_pay;
      _gstate.perblock_bucket     -= producer_per_block_pay;
      _gstate.total_unpaid_blocks -= prod.unpaid_blocks;

      /** 更新本节点的获取奖励时间以及出块数 */
      _producers.modify( prod, 0, [&](auto& p) {
          p.last_claim_time = ct;
          p.unpaid_blocks = 0;
      });

      /** 发起内联转账 */
      if( producer_per_block_pay > 0 ) {
         INLINE_ACTION_SENDER(eosio::token, transfer)( N(eosio.token), {N(eosio.bpay),N(active)},
                                                       { N(eosio.bpay), owner, asset(producer_per_block_pay), std::string("producer block pay") } );
      }
      if( producer_per_vote_pay > 0 ) {
         INLINE_ACTION_SENDER(eosio::token, transfer)( N(eosio.token), {N(eosio.vpay),N(active)},
                                                       { N(eosio.vpay), owner, asset(producer_per_vote_pay), std::string("producer vote pay") } );
      }
   }

} //namespace eosiosystem
