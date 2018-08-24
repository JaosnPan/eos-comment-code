#include <exchange/exchange_state.hpp>

namespace eosiosystem {
   /**
     * ����ת����RAM CODE
     * c��RAMCOREת��״̬������
     * in:�û��ĲƸ�
   */
   asset exchange_state::convert_to_exchange( connector& c, asset in ) {
      /** real_type��һ��˫���ȵĸ����� */
      /** RAMCORE�ķ������� */
      real_type R(supply.amount);
      /** RAM����ʷ�������� */
      real_type C(c.balance.amount+in.amount);
      /** F��һ��������Ȩ��0.5���Թ̶�Ϊ0.0005 */
      real_type F(c.weight/1000.0);
      /** ���ι����ڴ��EOS���� */
      real_type T(in.amount);
      real_type ONE(1.0);

      /**
       * ��ȡRAMCORE�Ĺ������ļ��㹫ʽ���£�
       *                 T
       * E = -R(1 - (1+ ---)^F)
       *                 C
      */
      real_type E = -R * (ONE - std::pow( ONE + T / C, F) );
      /** E���Ǳ��ι��������,ת����int64���� */
      //print( "E: ", E, "\n");
      int64_t issued = int64_t(E);
      /** RAMCORE����ʷ���������ӣ���Ӧ��Ҳͬ������ */
      supply.amount += issued;
      c.balance.amount += in.amount;

      /** ����asset(�Ƹ�)�ṹ����� */
      return asset( issued, supply.symbol );
   }

   /**
     * RAM COREת����RAM�����ʼ��㣩
     * c��ת��״̬������
     * in:�û��ĲƸ�
   */
   asset exchange_state::convert_from_exchange( connector& c, asset in ) {
      eosio_assert( in.symbol== supply.symbol, "unexpected asset symbol input" );
      /** RAMCORE�ķ������������Դ˴���Ҫ�����ν��׵�RAMCORE��ȥ���൱��RAMCORE����û�б仯 */
      real_type R(supply.amount - in.amount);
      /** RAM������ */
      real_type C(c.balance.amount);
      /** ������Ȩ��0.5������ĿǰΪ200 */
      real_type F(1000.0/c.weight);
      /** EOSת��ΪRAMCORE��RAMCORE�������� */
      real_type E(in.amount);
      
      real_type ONE(1.0);


     // potentially more accurate: 
     // The functions std::expm1 and std::log1p are useful for financial calculations, for example, 
     // when calculating small daily interest rates: (1+x)n
     // -1 can be expressed as std::expm1(n * std::log1p(x)). 
     // real_type T = C * std::expm1( F * std::log1p(E/R) );
     /**
      * ����ʹ��real_type T = C * std::expm1( F * std::log1p(E/R) );���������ʹ������ߣ��ڼ���ÿСʱ����ʱ
      */
     /**
      * ��ȡ����RAM������T�ļ��㹫ʽ���£�
      *            E
      * T = C((1+ ---)^F - 1)
      *            R
     */

      real_type T = C * (std::pow( ONE + E/R, F) - ONE);
      //print( "T: ", T, "\n");
      int64_t out = int64_t(T);
      /** RAMCORE����ʷ���������٣���Ӧ��Ҳͬ������ */
      supply.amount -= in.amount;
      c.balance.amount -= out;

      /** ����asset(�Ƹ�)�ṹ����� */
      return asset( out, c.balance.symbol );
   }

   /**
    * �ڴ湺��ת������
    * from: �Ƹ���Դ
    * to:   �������Դ����
   */
   asset exchange_state::convert( asset from, symbol_type to ) {
      auto sell_symbol  = from.symbol; /** Ҫ�����������ҵ��������ͣ�EOS��RAM�� */
      auto ex_symbol    = supply.symbol; /** RAMCORE�Ļ������� */
      auto base_symbol  = base.balance.symbol;  /** ��׼��Դ���ţ���ʵ����RAM��������\contracts\eosio.system\eosio.system.cpp��base.balance.symbol = S(0,RAM); */
      auto quote_symbol = quote.balance.symbol; /** ������Դ���� ��ʵ����EOS��������\contracts\eosio.system\eosio.system.cpp��quote.balance.symbol = CORE_SYMBOL;*/

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
