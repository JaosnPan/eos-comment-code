#include <exchange/exchange_state.hpp>

namespace eosiosystem {
   /**
     * 代币转换到RAM CODE
     * c：RAMCORE转化状态链接器
     * in:用户的财富
   */
   asset exchange_state::convert_to_exchange( connector& c, asset in ) {
      /** real_type是一个双精度的浮点数 */
      /** RAMCORE的发行总量 */
      real_type R(supply.amount);
      /** RAM的历史购买购买量 */
      real_type C(c.balance.amount+in.amount);
      /** F是一个常数，权重0.5所以固定为0.0005 */
      real_type F(c.weight/1000.0);
      /** 本次购买内存的EOS数量 */
      real_type T(in.amount);
      real_type ONE(1.0);

      /**
       * 获取RAMCORE的购买量的计算公式如下：
       *                 T
       * E = -R(1 - (1+ ---)^F)
       *                 C
      */
      real_type E = -R * (ONE - std::pow( ONE + T / C, F) );
      /** E就是本次购买的数量,转换成int64类型 */
      //print( "E: ", E, "\n");
      int64_t issued = int64_t(E);
      /** RAMCORE的历史购买量增加，供应量也同样增加 */
      supply.amount += issued;
      c.balance.amount += in.amount;

      /** 返回asset(财富)结构体对象 */
      return asset( issued, supply.symbol );
   }

   /**
     * RAM CORE转换到RAM（汇率计算）
     * c：转化状态链接器
     * in:用户的财富
   */
   asset exchange_state::convert_from_exchange( connector& c, asset in ) {
      eosio_assert( in.symbol== supply.symbol, "unexpected asset symbol input" );
      /** RAMCORE的发行总量，所以此处需要将本次交易的RAMCORE减去，相当于RAMCORE数量没有变化 */
      real_type R(supply.amount - in.amount);
      /** RAM的余量 */
      real_type C(c.balance.amount);
      /** 常亮，权重0.5，所以目前为200 */
      real_type F(1000.0/c.weight);
      /** EOS转换为RAMCORE后RAMCORE的增发量 */
      real_type E(in.amount);
      
      real_type ONE(1.0);


     // potentially more accurate: 
     // The functions std::expm1 and std::log1p are useful for financial calculations, for example, 
     // when calculating small daily interest rates: (1+x)n
     // -1 can be expressed as std::expm1(n * std::log1p(x)). 
     // real_type T = C * std::expm1( F * std::log1p(E/R) );
     /**
      * 可能使用real_type T = C * std::expm1( F * std::log1p(E/R) );这句代码可以使精度提高，在计算每小时利率时
      */
     /**
      * 获取正真RAM购买量T的计算公式如下：
      *            E
      * T = C((1+ ---)^F - 1)
      *            R
     */

      real_type T = C * (std::pow( ONE + E/R, F) - ONE);
      //print( "T: ", T, "\n");
      int64_t out = int64_t(T);
      /** RAMCORE的历史购买量减少，供应量也同样减少 */
      supply.amount -= in.amount;
      c.balance.amount -= out;

      /** 返回asset(财富)结构体对象 */
      return asset( out, c.balance.symbol );
   }

   /**
    * 内存购买转换函数
    * from: 财富来源
    * to:   购买的资源类型
   */
   asset exchange_state::convert( asset from, symbol_type to ) {
      auto sell_symbol  = from.symbol; /** 要参与买卖货币的输入类型（EOS、RAM） */
      auto ex_symbol    = supply.symbol; /** RAMCORE的货币类型 */
      auto base_symbol  = base.balance.symbol;  /** 基准资源符号，其实就是RAM，定义在\contracts\eosio.system\eosio.system.cpp：base.balance.symbol = S(0,RAM); */
      auto quote_symbol = quote.balance.symbol; /** 引用资源符号 其实就是EOS：定义在\contracts\eosio.system\eosio.system.cpp：quote.balance.symbol = CORE_SYMBOL;*/

      //print( "From: ", from, " TO ", asset( 0,to), "\n" );
      //print( "base: ", base_symbol, "\n" );
      //print( "quote: ", quote_symbol, "\n" );
      //print( "ex: ", supply.symbol, "\n" );

      if( sell_symbol != ex_symbol ) {
         if( sell_symbol == base_symbol ) {
            from = convert_to_exchange( base, from );
         } else if( sell_symbol == quote_symbol ) {
            from = convert_to_exchange( quote, from );
         } else { 
            eosio_assert( false, "invalid sell" );
         }
      } else {
         if( to == base_symbol ) {
            from = convert_from_exchange( base, from ); 
         } else if( to == quote_symbol ) {
            from = convert_from_exchange( quote, from ); 
         } else {
            eosio_assert( false, "invalid conversion" );
         }
      }

      if( to != from.symbol )
         return convert( from, to );

      return from;
   }



} /// namespace eosiosystem
