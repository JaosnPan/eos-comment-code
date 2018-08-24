
/**
 *  @file
 *  @copyright defined in eos/LICENSE.txt
 */
#pragma once

#include <eosio/chain/chain_config.hpp>
#include <eosio/chain/types.hpp>

#include <fc/crypto/sha256.hpp>

#include <string>
#include <vector>

namespace eosio { namespace chain {

struct genesis_state {
   genesis_state();

   static const string eosio_root_key;

   chain_config   initial_configuration = {
      .max_block_net_usage                  = config::default_max_block_net_usage,              /** 默认的区块最大网络使用量，默认为1024*1024，在500ms出一块，每个交易有200字节的情况下，可以支持到10,000 TPS的数据并发*/
      .target_block_net_usage_pct           = config::default_target_block_net_usage_pct,       /** 最大网络净使用量的目标百分比（1％== 100,100％= 10,000）;超过这会触发拥堵处理,默认为1000，也就是10%*/
      .max_transaction_net_usage            = config::default_max_transaction_net_usage,        /** 链允许的最大交易网络使用量，无论账户如何限制，默认为区块最大网络使用量的一半 */
      .base_per_transaction_net_usage       = config::default_base_per_transaction_net_usage,   /** 用于支付杂费的交易的基本净使用量, 默认为12个字节（对于transaction_receipt_header的最坏情况为11个字节，对于static_variant标记为1个字节）*/
      .net_usage_leeway                     = config::default_net_usage_leeway,                 /** 默认网络净使用余地，默认是500，是否合理不确定 */
      .context_free_discount_net_usage_num  = config::default_context_free_discount_net_usage_num, /** 无上下文数据净使用折扣的分子 */
      .context_free_discount_net_usage_den  = config::default_context_free_discount_net_usage_den, /** 无上下文数据净使用折扣的分母 */

      .max_block_cpu_usage                  = config::default_max_block_cpu_usage,                 /** 块的最大可计费cpu使用量（以微秒为单位）,默认为20w */
      .target_block_cpu_usage_pct           = config::default_target_block_cpu_usage_pct,          /** 最大CPU使用率的目标百分比（1％== 100,100％= 10,000）;超过这会触发拥堵处理,默认为10% */
      .max_transaction_cpu_usage            = config::default_max_transaction_cpu_usage,           /** 无论帐户限制如何，链将允许的最大可计费cpu使用量（以微秒为单位）,默认为最大可计费使用量的75% */
      .min_transaction_cpu_usage            = config::default_min_transaction_cpu_usage,           /** 链所需的最小可计费cpu使用量（以微秒为单位）,默认为100，和10000 TPS等值 */

      .max_transaction_lifetime             = config::default_max_trx_lifetime,                    /** 输入事务到期的最大秒数可以超过首次包含它的块的时间,默认是10小时 */
      .deferred_trx_expiration_window       = config::default_deferred_trx_expiration_window,      /** 延迟事务首次执行到期的秒数，默认为10分钟 */
      .max_transaction_delay                = config::default_max_trx_delay,                       /** 授权检查可以作为延迟要求施加的最大秒数, 默认是45天 */
      .max_inline_action_size               = config::default_max_inline_action_size,              /** 内联操作的最大允许大小（以字节为单位,默认是4kb */
      .max_inline_action_depth              = config::default_max_inline_action_depth,             /** 发送内联操作的递归深度限制,默认为4 */
      .max_authority_depth                  = config::default_max_auth_depth,                      /** 用于检查权限是否满足的递归深度限制,默认为6 */
   };

   time_point                               initial_timestamp;    /** 初始化时间戳 */
   public_key_type                          initial_key;          /** 初始化密钥（EOSIO公钥） */

   /**
    * Get the chain_id corresponding to this genesis state.
    *
    * This is the SHA256 serialization of the genesis_state.
    */
   chain_id_type compute_chain_id() const;   /** 计算链id */
};

} } // namespace eosio::chain


FC_REFLECT(eosio::chain::genesis_state,
           (initial_timestamp)(initial_key)(initial_configuration))
