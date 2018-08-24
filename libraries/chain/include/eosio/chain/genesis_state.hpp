
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
      .max_block_net_usage                  = config::default_max_block_net_usage,              /** Ĭ�ϵ������������ʹ������Ĭ��Ϊ1024*1024����500ms��һ�飬ÿ��������200�ֽڵ�����£�����֧�ֵ�10,000 TPS�����ݲ���*/
      .target_block_net_usage_pct           = config::default_target_block_net_usage_pct,       /** ������羻ʹ������Ŀ��ٷֱȣ�1��== 100,100��= 10,000��;������ᴥ��ӵ�´���,Ĭ��Ϊ1000��Ҳ����10%*/
      .max_transaction_net_usage            = config::default_max_transaction_net_usage,        /** ����������������ʹ�����������˻�������ƣ�Ĭ��Ϊ�����������ʹ������һ�� */
      .base_per_transaction_net_usage       = config::default_base_per_transaction_net_usage,   /** ����֧���ӷѵĽ��׵Ļ�����ʹ����, Ĭ��Ϊ12���ֽڣ�����transaction_receipt_header������Ϊ11���ֽڣ�����static_variant���Ϊ1���ֽڣ�*/
      .net_usage_leeway                     = config::default_net_usage_leeway,                 /** Ĭ�����羻ʹ����أ�Ĭ����500���Ƿ����ȷ�� */
      .context_free_discount_net_usage_num  = config::default_context_free_discount_net_usage_num, /** �����������ݾ�ʹ���ۿ۵ķ��� */
      .context_free_discount_net_usage_den  = config::default_context_free_discount_net_usage_den, /** �����������ݾ�ʹ���ۿ۵ķ�ĸ */

      .max_block_cpu_usage                  = config::default_max_block_cpu_usage,                 /** ������ɼƷ�cpuʹ��������΢��Ϊ��λ��,Ĭ��Ϊ20w */
      .target_block_cpu_usage_pct           = config::default_target_block_cpu_usage_pct,          /** ���CPUʹ���ʵ�Ŀ��ٷֱȣ�1��== 100,100��= 10,000��;������ᴥ��ӵ�´���,Ĭ��Ϊ10% */
      .max_transaction_cpu_usage            = config::default_max_transaction_cpu_usage,           /** �����ʻ�������Σ�������������ɼƷ�cpuʹ��������΢��Ϊ��λ��,Ĭ��Ϊ���ɼƷ�ʹ������75% */
      .min_transaction_cpu_usage            = config::default_min_transaction_cpu_usage,           /** ���������С�ɼƷ�cpuʹ��������΢��Ϊ��λ��,Ĭ��Ϊ100����10000 TPS��ֵ */

      .max_transaction_lifetime             = config::default_max_trx_lifetime,                    /** ���������ڵ�����������Գ����״ΰ������Ŀ��ʱ��,Ĭ����10Сʱ */
      .deferred_trx_expiration_window       = config::default_deferred_trx_expiration_window,      /** �ӳ������״�ִ�е��ڵ�������Ĭ��Ϊ10���� */
      .max_transaction_delay                = config::default_max_trx_delay,                       /** ��Ȩ��������Ϊ�ӳ�Ҫ��ʩ�ӵ��������, Ĭ����45�� */
      .max_inline_action_size               = config::default_max_inline_action_size,              /** ������������������С�����ֽ�Ϊ��λ,Ĭ����4kb */
      .max_inline_action_depth              = config::default_max_inline_action_depth,             /** �������������ĵݹ��������,Ĭ��Ϊ4 */
      .max_authority_depth                  = config::default_max_auth_depth,                      /** ���ڼ��Ȩ���Ƿ�����ĵݹ��������,Ĭ��Ϊ6 */
   };

   time_point                               initial_timestamp;    /** ��ʼ��ʱ��� */
   public_key_type                          initial_key;          /** ��ʼ����Կ��EOSIO��Կ�� */

   /**
    * Get the chain_id corresponding to this genesis state.
    *
    * This is the SHA256 serialization of the genesis_state.
    */
   chain_id_type compute_chain_id() const;   /** ������id */
};

} } // namespace eosio::chain


FC_REFLECT(eosio::chain::genesis_state,
           (initial_timestamp)(initial_key)(initial_configuration))
