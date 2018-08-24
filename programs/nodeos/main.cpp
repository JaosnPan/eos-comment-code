/**
 *  @file
 *  @copyright defined in eosio/LICENSE.txt
 */
#include <appbase/application.hpp>

#include <eosio/chain_plugin/chain_plugin.hpp>
#include <eosio/http_plugin/http_plugin.hpp>
#include <eosio/history_plugin/history_plugin.hpp>
#include <eosio/net_plugin/net_plugin.hpp>
#include <eosio/producer_plugin/producer_plugin.hpp>
#include <eosio/utilities/common.hpp>

#include <fc/log/logger_config.hpp>
#include <fc/log/appender.hpp>
#include <fc/exception/exception.hpp>

#include <boost/dll/runtime_symbol_info.hpp>
#include <boost/exception/diagnostic_information.hpp>

#include "config.hpp"

using namespace appbase;
using namespace eosio;

namespace fc {
   std::unordered_map<std::string,appender::ptr>& get_appender_map();
}

namespace detail {

void configure_logging(const bfs::path& config_path)
{
   try {
      try {
         fc::configure_logging(config_path);
      } catch (...) {
         elog("Error reloading logging.json");
         throw;
      }
   } catch (const fc::exception& e) {
      elog("${e}", ("e",e.to_detail_string()));
   } catch (const boost::exception& e) {
      elog("${e}", ("e",boost::diagnostic_information(e)));
   } catch (const std::exception& e) {
      elog("${e}", ("e",e.what()));
   } catch (...) {
      // empty
   }
}

} // namespace detail

void logging_conf_loop()
{
   std::shared_ptr<boost::asio::signal_set> sighup_set(new boost::asio::signal_set(app().get_io_service(), SIGHUP));
   sighup_set->async_wait([sighup_set](const boost::system::error_code& err, int /*num*/) {
      if(!err)
      {
         ilog("Received HUP.  Reloading logging configuration.");
         auto config_path = app().get_logging_conf();
         if(fc::exists(config_path))
            ::detail::configure_logging(config_path);
         for(auto iter : fc::get_appender_map())
            iter.second->initialize(app().get_io_service());
         logging_conf_loop();
      }
   });
}

void initialize_logging()
{
   auto config_path = app().get_logging_conf();
   if(fc::exists(config_path))
     fc::configure_logging(config_path); // intentionally allowing exceptions to escape
   for(auto iter : fc::get_appender_map())
     iter.second->initialize(app().get_io_service());

   logging_conf_loop();
}

enum return_codes {
   OTHER_FAIL        = -2,
   INITIALIZE_FAIL   = -1,
   SUCCESS           = 0,
   BAD_ALLOC         = 1,
   DATABASE_DIRTY    = 2,
   FIXED_REVERSIBLE  = 3,
   EXTRACTED_GENESIS = 4,
   NODE_MANAGEMENT_SUCCESS = 5
};
/**
    * @brief nodeos程序的主入口 \n
    * nodeos运行依赖appbase插件管理类
    * @param[in] argc   参数个数
    * @param[in] argv   参数列表
    * @return 函数的返回值，具体参见枚举return_codes
    * @see appbase::application
    */
int main(int argc, char** argv)
{
   /**
    * 为了实现数据的共享，在appbase中实现了一个静态函数instance，
    * 在instance函数中声明了一个_app的静态对象，在类声明头文件中声明了_app的访问接口，
    * 在实现文件中进行了初始化，对外访问时提供一个对象引用
   */
   try {
      /**
       * 设置applet的版本号和UUID
       * 这个版本号定义在文件夹下的config.hpp.in中，
       * 注意constexpr修饰词，意味着该常量变量是在编译期就应该确定该值的内容
      */
      app().set_version(eosio::nodeos::config::version);
      /**
       * 插件是以模板的形式存在，使用流程一般有以下几个操作
       * 1、注册插件
       * 2、初始化插件
       * 3、启动插件
       * 4、关闭插件
       * 插件注册完成会将插件对象插入到app对象中私有成员map<string, std::unique_ptr<abstract_plugin>> plugins;中
       * TIPS:以下的一句代码其实没什么意义，因为在所有的可用插件中cpp文件中都会有静态成员声明，例如：
       *    namespace eosio {
       *       static appbase::abstract_plugin& _net_plugin = app().register_plugin<net_plugin>();
       *    也就意味着程序进入到main之前，支持的插件都已经注册完成了
      */
      app().register_plugin<history_plugin>();

      /**
       * 非windeow平台从环境变量中获取程序运行的根目录并设置配置文件以及数据存储的目录
      */
      auto root = fc::app_path();
      app().set_default_data_dir(root / "eosio/nodeos/data" );
      app().set_default_config_dir(root / "eosio/nodeos/config" );
      /**默认调用libraries/appbase/include/appbase/application.hpp中的initialize函数进行插件的初始化，
       * 在对应的cpp中有对应的实现接口initialize_impl
       * 默认情况下，nodeos启动时加载链插件、http插件、net（点对点）插件、生产者插件
       * 自动加载的插件是以一个vector的abstract_plugin指针形式存在并传入实现接口initialize_impl中
       * 在initialize_impl中使用boost::program_options命令行参数解析配置选项对app进行配置
       * 调用abstract_plugin函数中的initialize接口对每一个插件进行初始化
      */
      if(!app().initialize<chain_plugin, http_plugin, net_plugin, producer_plugin>(argc, argv))
         return INITIALIZE_FAIL;
      /**
       * 初始化log
       * 打印nodeos的版本以及nodeos的配置根目录
      */
      initialize_logging();
      
      ilog("nodeos version ${ver}", ("ver", eosio::utilities::common::itoh(static_cast<uint32_t>(app().version()))));
      ilog("eosio root is ${root}", ("root", root.string()));

      /**
       * 循环获取initialized_plugins中存储的插件并执行plugin->startup()函数
       * 如果在执行过程中出现异常，则调用application::shutdown()进行插件的关闭处理
      */
      app().startup();
      /**
       * 执行exec函数，完成信号回调函数设置，和消息循环(io_serv->run())
      */
      app().exec();
   } catch( const extract_genesis_state_exception& e ) {
      return EXTRACTED_GENESIS;
   } catch( const fixed_reversible_db_exception& e ) {
      return FIXED_REVERSIBLE;
   } catch( const node_management_success& e ) {
      return NODE_MANAGEMENT_SUCCESS;
   } catch( const fc::exception& e ) {
      if( e.code() == fc::std_exception_code ) {
         if( e.top_message().find( "database dirty flag set" ) != std::string::npos ) {
            elog( "database dirty flag set (likely due to unclean shutdown): replay required" );
            return DATABASE_DIRTY;
         } else if( e.top_message().find( "database metadata dirty flag set" ) != std::string::npos ) {
            elog( "database metadata dirty flag set (likely due to unclean shutdown): replay required" );
            return DATABASE_DIRTY;
         }
      }
      elog( "${e}", ("e", e.to_detail_string()));
      return OTHER_FAIL;
   } catch( const boost::interprocess::bad_alloc& e ) {
      elog("bad alloc");
      return BAD_ALLOC;
   } catch( const boost::exception& e ) {
      elog("${e}", ("e",boost::diagnostic_information(e)));
      return OTHER_FAIL;
   } catch( const std::runtime_error& e ) {
      if( std::string(e.what()) == "database dirty flag set" ) {
         elog( "database dirty flag set (likely due to unclean shutdown): replay required" );
         return DATABASE_DIRTY;
      } else if( std::string(e.what()) == "database metadata dirty flag set" ) {
         elog( "database metadata dirty flag set (likely due to unclean shutdown): replay required" );
         return DATABASE_DIRTY;
      } else {
         elog( "${e}", ("e",e.what()));
      }
      return OTHER_FAIL;
   } catch( const std::exception& e ) {
      elog("${e}", ("e",e.what()));
      return OTHER_FAIL;
   } catch( ... ) {
      elog("unknown exception");
      return OTHER_FAIL;
   }

   return SUCCESS;
}
