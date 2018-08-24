/**
 *  @file
 *  @copyright defined in eos/LICENSE.txt
 */
#include "eosio.system.hpp"

#include <eosiolib/eosio.hpp>
#include <eosiolib/crypto.h>
#include <eosiolib/print.hpp>
#include <eosiolib/datastream.hpp>
#include <eosiolib/serialize.hpp>
#include <eosiolib/multi_index.hpp>
#include <eosiolib/privileged.hpp>
#include <eosiolib/singleton.hpp>
#include <eosiolib/transaction.hpp>
#include <eosio.token/eosio.token.hpp>

#include <algorithm>
#include <cmath>

namespace eosiosystem {
   using eosio::indexed_by;
   using eosio::const_mem_fun;
   using eosio::bytes;
   using eosio::print;
   using eosio::singleton;
   using eosio::transaction;

   /**
    *  This method will create a producer_config and producer_info object for 'producer'
    *  此方法将创建矿工对象
    *
    *  @pre producer is not already registered
    *       账户尚未注册成为矿工
    *  @pre producer to register is an account
    *       要注册的生产着是一个账户
    *  @pre authority of producer to register
    *       注册需要生产者的授权
    */
   void system_contract::regproducer( const account_name producer, const eosio::public_key& producer_key, const std::string& url, uint16_t location ) {
      /** url不能超过512字节，公钥必须是合法的公钥，必须进行授权 */
      eosio_assert( url.size() < 512, "url too long" );
      eosio_assert( producer_key != eosio::public_key(), "public key should not be the default value" );
      require_auth( producer );

      auto prod = _producers.find( producer );
      /** 在矿工表中查找是否存在该矿工，如果存在则更新公钥,有效性为真，更新location和url */
      if ( prod != _producers.end() ) {
         _producers.modify( prod, producer, [&]( producer_info& info ){
               info.producer_key = producer_key;
               info.is_active    = true;
               info.url          = url;
               info.location     = location;
            });
      } else {
         /** 如果不存在则使用emplace接口构造并更新对应的值，注意投票数初始化成0 */
         _producers.emplace( producer, [&]( producer_info& info ){
               info.owner         = producer;
               info.total_votes   = 0;
               info.producer_key  = producer_key;
               info.is_active     = true;
               info.url           = url;
               info.location      = location;
         });
      }
   }

   void system_contract::unregprod( const account_name producer ) {
      /** 验证授权，如果矿工存在于矿工表中则解除矿工，即将active设置为false */
      require_auth( producer );
      
      const auto& prod = _producers.get( producer, "producer not found" );

      _producers.modify( prod, 0, [&]( producer_info& info ){
            info.deactivate();
      });
   }

   /** 
     * 更新当选者矿工列表
     * 参数：块时间戳
     */
   void system_contract::update_elected_producers( block_timestamp block_time ) {
      /** 更新全局参数中上次生产者计划更新时间为块的时间戳 */
      _gstate.last_producer_schedule_update = block_time;
      
      auto idx = _producers.get_index<N(prototalvote)>();
      /** 创建生产者向量并预留21个对象，目前为21个生产者 */
      std::vector< std::pair<eosio::producer_key,uint16_t> > top_producers;
      top_producers.reserve(21);
      /** 在_producers多索引表中查找投票数非0并且是有效的矿工追加到top_producers向量表中 */
      for ( auto it = idx.cbegin(); it != idx.cend() && top_producers.size() < 21 && 0 < it->total_votes && it->active(); ++it ) {
         top_producers.emplace_back( std::pair<eosio::producer_key,uint16_t>({{it->owner, it->producer_key}, it->location}) );
      }

      /** 如果提取出来的矿工数比上次计划更新的矿工数小，则直接退出 */
      if ( top_producers.size() < _gstate.last_producer_schedule_size ) {
         return;
      }

      /** 按照名称进行排序并将公钥列表进行打包赋值给packed_schedule串 */
      /// sort by producer name
      std::sort( top_producers.begin(), top_producers.end() );

      std::vector<eosio::producer_key> producers;

      producers.reserve(top_producers.size());
      for( const auto& item : top_producers )
         producers.push_back(item.first);

      bytes packed_schedule = pack(producers);
      /** 提交建议生产者更改计划, 成功返回非-1和0 */
      if( set_proposed_producers( packed_schedule.data(),  packed_schedule.size() ) >= 0 ) {
         /** 如果成功则将top_producers.size()的值解析成_gstate.last_producer_schedule_size的类型赋值给_gstate.last_producer_schedule_size */
         _gstate.last_producer_schedule_size = static_cast<decltype(_gstate.last_producer_schedule_size)>( top_producers.size() );
      }
   }

   /** 计算新的抵押投票权重 */
   double stake2vote( int64_t staked ) {
      /// TODO subtract 2080 brings the large numbers closer to this decade
      /** 
       * 投票抵押权重的计算公式: 权重 = [当前相对unix的纪元时间-区块链时间戳纪元（2000年）的日期的周数]/52
       * 抵押投票的权重 = 抵押的票数 * 2的权重次方
       */
      double weight = int64_t( (now() - (block_timestamp::block_timestamp_epoch / 1000)) / (seconds_per_day * 7) )  / double( 52 );
      return double(staked) * std::pow( 2, weight );
   }
   /**
    *  @pre producers must be sorted from lowest to highest and must be registered and active
    *       必须从最低到最高排序，并且必须注册并激活
    *  @pre if proxy is set then no producers can be voted for
    *       如果设置了代理，则不能投票给任何生产者
    *  @pre if proxy is set then proxy account must exist and be registered as a proxy
    *       如果设置了代理，则代理帐户必须存在并注册为代理
    *  @pre every listed producer or proxy must have been previously registered
    *       每个在列表中的生产者或代理人必须先前已经注册
    *  @pre voter must authorize this action
    *       投票人必须授权此操作
    *  @pre voter must have previously staked some EOS for voting
    *       投票人必须事先抵押一些EOS进行投票
    *  @pre voter->staked must be up to date
    *       voter-> staked必须是最新的
    *  @post every producer previously voted for will have vote reduced by previous vote weight
    *       之前投票的每个生产者都将通过之前的投票权重减少投票
    *  @post every producer newly voted for will have vote increased by new vote amount
    *       新投票的每个生产者都将以新的投票金额增加投票
    *  @post prior proxy will proxied_vote_weight decremented by previous vote weight
    *       先前代理将proxied_vote_weight减去之前的投票权重
    *  @post new proxy will proxied_vote_weight incremented by new vote weight
    *       新的代理将proxied_vote_weight增加新的投票权重
    *  If voting for a proxy, the producer votes will not change until the proxy updates their own vote.
    *       如果向代理投票，生产者投票将不会改变，直到代理更新自己的投票
    */

   /**
   * 投票给生产者
   * voter_name:投票人名称
   * proxy:代理名称
   * producers:生产者列表
   */
   void system_contract::voteproducer( const account_name voter_name, const account_name proxy, const std::vector<account_name>& producers ) {
      /** 验证投票者权限，投票 */
      require_auth( voter_name );
      update_votes( voter_name, proxy, producers, true );
   }

   void system_contract::update_votes( const account_name voter_name, const account_name proxy, const std::vector<account_name>& producers, bool voting ) {
      //validate input
      if ( proxy ) {
         /** 使用代理投票时，生产者列表必须为空并且不能自己投自己，验证代理是否在于提供的验证集中 */
         eosio_assert( producers.size() == 0, "cannot vote for producers and proxy at same time" );
         eosio_assert( voter_name != proxy, "cannot proxy to self" );
         require_recipient( proxy );
      } else {
         /** 不适用代理投票则一票只能最多投给30个节点，并且生产者列表必须是有序的 */
         eosio_assert( producers.size() <= 30, "attempt to vote for too many producers" );
         for( size_t i = 1; i < producers.size(); ++i ) {
            eosio_assert( producers[i-1] < producers[i], "producer votes must be unique and sorted" );
         }
      }

      /**
       * 根据投票者名称在投票者表中查询
       * 如果投票者未抵押（不在投票者列表中）或者投票者是一个代理，不允许投票
      */
      auto voter = _voters.find(voter_name);
      eosio_assert( voter != _voters.end(), "user must stake before they can vote" ); /// staking creates voter object
      eosio_assert( !proxy || !voter->is_proxy, "account registered as a proxy is not allowed to use a proxy" );

      /**
       * The first time someone votes we calculate and set last_vote_weight, since they cannot unstake until
       * after total_activated_stake hits threshold, we can use last_vote_weight to determine that this is
       * their first vote and should consider their stake activated.
       * 第一次有人投票我们计算并设置last_vote_weight，因为他们不能在total_activated_stake达到阈值之前取消，
       * 我们可以使用last_vote_weight来确定这是他们的第一次投票，并且应该考虑他们的抵押被激活
       */
      if( voter->last_vote_weight <= 0.0 ) {
         _gstate.total_activated_stake += voter->staked; /** 总抵押数增加 */
         if( _gstate.total_activated_stake >= min_activated_stake && _gstate.thresh_activated_stake_time == 0 ) {
            _gstate.thresh_activated_stake_time = current_time(); /** 如果总抵押数大于最小抵押数（%15）,并且全局抵押激活时间为0则设置当前时间为抵押激活时间 */
         }
      }

      /** 计算新的抵押权重，如果投票者是代理则需要将该投票者的代理投票权重增加到当前投票权重上 */
      auto new_vote_weight = stake2vote( voter->staked );
      if( voter->is_proxy ) {
         new_vote_weight += voter->proxied_vote_weight;
      }

      boost::container::flat_map<account_name, pair<double, bool /*new*/> > producer_deltas;
      /** 如果投票者的最后抵押权重大于0,则需要计算新的投票权重 */
      if ( voter->last_vote_weight > 0 ) {
         if( voter->proxy ) {
            /** 如果投票者设置了代理并且该存在于投票者列表中，修改投票权重，即代理人的总投票权重减去最后一次投票的权重，并且在propagate_weight_change中更新 */
            auto old_proxy = _voters.find( voter->proxy );
            eosio_assert( old_proxy != _voters.end(), "old proxy not found" ); //data corruption
            _voters.modify( old_proxy, 0, [&]( auto& vp ) {
                  vp.proxied_vote_weight -= voter->last_vote_weight;
               });
            /** 传播新的代理投票者权重            */
            propagate_weight_change( *old_proxy );
         } else {
            /** 如果投票者未设置代理，并且以前投过票，也就是最后的投票权重大于0，那么将投票者投过的每一个生产者的投票权重减去，并且将投票状态设置为未投票并暂存 */
            for( const auto& p : voter->producers ) {
               auto& d = producer_deltas[p];
               d.first -= voter->last_vote_weight;
               d.second = false;
            }
         }
      }

      if( proxy ) {
         /** 如果委托了代理者，代理者必须在投票者表中并且指定的代理账户必须是一个有效的代理 */
         auto new_proxy = _voters.find( proxy );
         eosio_assert( new_proxy != _voters.end(), "invalid proxy specified" ); //if ( !voting ) { data corruption } else { wrong vote }
         eosio_assert( !voting || new_proxy->is_proxy, "proxy not found" );
         if ( new_vote_weight >= 0 ) {
            /** 如果投票者新的权重大于0，则将新的权重更新给投票者并传播播投票者的权重 */
            _voters.modify( new_proxy, 0, [&]( auto& vp ) {
                  vp.proxied_vote_weight += new_vote_weight;
               });
            propagate_weight_change( *new_proxy );
         }
      } else {
         /** 如果未委托代理，那么更新生产者暂存表producer_deltas中的每一个生产者的权重并更新投票状态 */
         if( new_vote_weight >= 0 ) {
            for( const auto& p : producers ) {
               auto& d = producer_deltas[p];
               d.first += new_vote_weight;
               d.second = true;
            }
         }
      }

      /** 遍历暂存生产者表，更新得票生产者的得票权重 */
      for( const auto& pd : producer_deltas ) {
         /** 根据暂存表中的信息，查找生产者表 */
         auto pitr = _producers.find( pd.first );
         /** 被投票的生产者在投者列表中 */
         if( pitr != _producers.end() ) {
            /** 判断voting必须为true，其实调用时已经写死, 生产者必须是一个有效的生产者, 投票者不能是一个代理 */   
            eosio_assert( !voting || pitr->active() || !pd.second.second /* not from new set */, "producer is not currently registered" );
            /** 更新该生产者的票数并修改目前主网中的投票比重 */
            _producers.modify( pitr, 0, [&]( auto& p ) {
               p.total_votes += pd.second.first;
               if ( p.total_votes < 0 ) { // floating point arithmetics can give small negative numbers
                  p.total_votes = 0;
               }
               _gstate.total_producer_vote_weight += pd.second.first;
               //eosio_assert( p.total_votes >= 0, "something bad happened" );
            });
         } else {
            /** 被投票者的不在生产者列表中则报错 */
            eosio_assert( !pd.second.second /* not from new set */, "producer is not registered" ); //data corruption
         }
      }
      /** 更新最新的投票者信息 */
      _voters.modify( voter, 0, [&]( auto& av ) {
         av.last_vote_weight = new_vote_weight;
         av.producers = producers;
         av.proxy     = proxy;
      });
   }

   /**
    *  An account marked as a proxy can vote with the weight of other accounts which
    *  have selected it as a proxy. Other accounts must refresh their voteproducer to
    *  update the proxy's weight.
    *
    *  标记为代理的帐户可以使用其他帐户的权重进行投票已选择它作为代理。 其他帐户必须刷新他们的投票生产者更新代理的权重。
    *  @param isproxy - true if proxy wishes to vote on behalf of others, false otherwise
    *                   如果代理人希望代表他人投票，则为true，否则为false
    *  @pre proxy must have something staked (existing row in voters table)
    *       代理必须有一些抵押（选民表中的现有行）
    *  @pre new state must be different than current state
    *       新状态必须与当前状态不同
    */
   void system_contract::regproxy( const account_name proxy, bool isproxy ) {
      require_auth( proxy );

      /** 要想注册代理人/解职代理人，那么账户如果在必须在投票者列表中 */
      auto pitr = _voters.find(proxy);
      if ( pitr != _voters.end() ) {
         eosio_assert( isproxy != pitr->is_proxy, "action has no effect" );
         /** 状态一致抛出异常 */
         eosio_assert( !isproxy || !pitr->proxy, "account that uses a proxy is not allowed to become a proxy" );
         /** 更新投票者状态 */
         _voters.modify( pitr, 0, [&]( auto& p ) {
               p.is_proxy = isproxy;
            });
            
         /** 更新投票者权重 */
         propagate_weight_change( *pitr );
      } else {
         /** 如果投票人不在投票者表中，如果想成为代理或者解职代理，在投票者表中增加一列 */
         _voters.emplace( proxy, [&]( auto& p ) {
               p.owner  = proxy;
               p.is_proxy = isproxy;
            });
      }
   }

   /** 广播权重 */
   void system_contract::propagate_weight_change( const voter_info& voter ) {
      /** 投票者注册成为一个代理但是不允许使用代理抛出异常 */
      eosio_assert( voter.proxy == 0 || !voter.is_proxy, "account registered as a proxy is not allowed to use a proxy" );

      /** 计算新的权重， 如果投票者是代理，新的权重需要加上原有的投票权重 */
      double new_weight = stake2vote( voter.staked );
      if ( voter.is_proxy ) {
         new_weight += voter.proxied_vote_weight;
      }

      /// don't propagate small changes (1 ~= epsilon) /** 不广播小的变化，小的变量约等于1， 意味着你新增加的投票权重如果绝对值小于1，那么不去修改_gstate.total_producer_vote_weight的权重 */
      if ( fabs( new_weight - voter.last_vote_weight ) > 1 )  {
         if ( voter.proxy ) {
            /** 如果委托给投票代理人投票，首先在投票者中查询是否存在投票代理人，如果存在投票代理人的投票权重为投票人的新投票权重减去该投票人的最终投票权重
              * 意思就如果本次投票投票者委托给了代理人，那么这次不会增加你的投票权重，而是这次新增的投票权重增加给代理人
            */
            auto& proxy = _voters.get( voter.proxy, "proxy not found" ); //data corruption
            _voters.modify( proxy, 0, [&]( auto& p ) {
                  p.proxied_vote_weight += new_weight - voter.last_vote_weight;
               }
            );
            /** 广播代理者的投票权重 */
            propagate_weight_change( proxy );
         } else {
            /** 更新每一个生产者的得票量并更新所有生产者的得票权重 */
            auto delta = new_weight - voter.last_vote_weight;
            for ( auto acnt : voter.producers ) {
               auto& pitr = _producers.get( acnt, "producer not found" ); //data corruption
               _producers.modify( pitr, 0, [&]( auto& p ) {
                     p.total_votes += delta;
                     _gstate.total_producer_vote_weight += delta;
               });
            }
         }
      }
      /** 更新最终的投票权重为新的投票权重 */
      _voters.modify( voter, 0, [&]( auto& v ) {
            v.last_vote_weight = new_weight;
         }
      );
   }

} /// namespace eosiosystem
