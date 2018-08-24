#include <appbase/application.hpp>

#include <boost/algorithm/string.hpp>
#include <boost/filesystem.hpp>
#include <boost/asio/signal_set.hpp>
#include <boost/algorithm/string.hpp>

#include <iostream>
#include <fstream>

namespace appbase {

namespace bpo = boost::program_options;
using bpo::options_description;
using bpo::variables_map;
using std::cout;

class application_impl {
   public:
      application_impl():_app_options("Application Options"){
      }
      const variables_map*    _options = nullptr;
      options_description     _app_options;
      options_description     _cfg_options;

      bfs::path               _data_dir{"data-dir"};
      bfs::path               _config_dir{"config-dir"};
      bfs::path               _logging_conf{"logging.json"};

      uint64_t                _version;
      std::string             _uuid;
};

application::application()
:my(new application_impl()){
   io_serv = std::make_shared<boost::asio::io_service>();
}

application::~application() { }

void application::set_version(uint64_t version) {
  my->_version = version;
}

uint64_t application::version() const {
  return my->_version;
}

void application::set_uuid(const string& uuid) {
  my->_uuid = uuid;
}

string application::uuid() const {
  return my->_uuid;
}

void application::set_default_data_dir(const bfs::path& data_dir) {
  my->_data_dir = data_dir;
}

void application::set_default_config_dir(const bfs::path& config_dir) {
  my->_config_dir = config_dir;
}

bfs::path application::get_logging_conf() const {
  return my->_logging_conf;
}
/**
 * @brief 启动初始化的插件
 * @param none
 * @return none
*/
void application::startup() {
   /**
    * 遍历initialized_plugins类成员，调用每个插件的startup函数
    * 注意：每个插件的startup继承自plugin模板类(application.hpp)，plugin类继承自abstract_plugin类(plugin.hpp),plugin类重写了startup虚函数，所以有可能某些
    * 插件没有自定义功能，所以也没有重写的startup,直接执行plugin类的启动函数
   */
   try {
      for (auto plugin : initialized_plugins)
         plugin->startup();
   } catch(...) {
      shutdown();
      throw;
   }
}

application& application::instance() {
   static application _app;
   return _app;
}
application& app() { return application::instance(); }

/**
 * @brief 设置注册插件、默认的程序命令行参数解析配置选项
 * @param none
 * @return none
*/
void application::set_program_options()
{
   /**将类成员plugins使用别名plug进行循环访问，设置每一个插件的选项描述
    * 这是c++11的一个特性，注意引用&，&意味者可以改变对应的值，for循环中的每个迭代初始化一个新的引用
    * for (auto it = plug.begin(); it != plug.end(); ++it)
    * {
    *    auto &plug = *it;
    * }
    */
   for(auto& plug : plugins) {
      /**
       * 1、声明两个选项描述器：
       *    命令行参数描述器
       *    配置选项描述器
       * 2、调用插件的实现类的set_program_options方法进行设置程序选项，注意每一个插件是application中plugin的模板，plugin继承自abstract_plugin类
       * abstract_plugin类声明了set_program_options虚函数，所以有可能某个插件子类中未重写这个函数，所以对于不同的插件可能没有配置参数选项或者命令行参数
       * 选项
       * 3、如果具体的插件在重写的set_program_options方法中增加了配置则需要在application类的成员命令行参数和配置参数中增加命令行参数。
       * 4、注意unique_ptr指针my的声明，这个时c++11的特性，用于将选项描述器对象移交给my对应的对象。
      */
      boost::program_options::options_description plugin_cli_opts("Command Line Options for " + plug.second->name());
      boost::program_options::options_description plugin_cfg_opts("Config Options for " + plug.second->name());
      plug.second->set_program_options(plugin_cli_opts, plugin_cfg_opts);
      if(plugin_cfg_opts.options().size()) {
         my->_app_options.add(plugin_cfg_opts);
         my->_cfg_options.add(plugin_cfg_opts);
      }
      if(plugin_cli_opts.options().size())
         my->_app_options.add(plugin_cli_opts);
   }

   /**
    * 1、声明两个选项描述器
    *    应用的命令行参数选项
    *    应用的配置参数选项
    * 2、与插件命令行参数以及配置参数处理一样，将对应新增的命令行参数和应用配置参数对象移交给my对应的对象。
    * 
   */
   options_description app_cfg_opts( "Application Config Options" );
   options_description app_cli_opts( "Application Command Line Options" );
   app_cfg_opts.add_options()
         ("plugin", bpo::value< vector<string> >()->composing(), "Plugin(s) to enable, may be specified multiple times");

   app_cli_opts.add_options()
         ("help,h", "Print this help message and exit.")
         ("version,v", "Print version information.")
         ("uuid", "Print uuid of program.")
         ("print-default-config", "Print default configuration template")
         ("data-dir,d", bpo::value<std::string>(), "Directory containing program runtime data")
         ("config-dir", bpo::value<std::string>(), "Directory containing configuration files such as config.ini")
         ("config,c", bpo::value<std::string>()->default_value( "config.ini" ), "Configuration file name relative to config-dir")
         ("logconf,l", bpo::value<std::string>()->default_value( "logging.json" ), "Logging configuration file name/path for library users");

   my->_cfg_options.add(app_cfg_opts);
   my->_app_options.add(app_cfg_opts);
   my->_app_options.add(app_cli_opts);
}
/**
    * @brief 插件初始化实现函数
    * @param[in] argc   参数个数
    * @param[in] argv   参数列表
    * @param[in] autostart_plugins  自启动插件对象指针向量
    * @return true/false
    */
bool application::initialize_impl(int argc, char** argv, vector<abstract_plugin*> autostart_plugins) {
   set_program_options(); /**设置注册插件、默认的程序命令行参数解析配置选项*/

   /**
    * 1、声明一个选项存储map容器
    * 2、将上层传入的命令行参数进行解析并将解析结果存储在options容器中
   */
   bpo::variables_map options;
   bpo::store(bpo::parse_command_line(argc, argv, my->_app_options), options);

   /**
    * 解析完成，根据对应的参数进行处理
   */

   /**help选项：打印_app_options中的所有内容*/
   if( options.count( "help" ) ) {
      cout << my->_app_options << std::endl;
      return false;
   }
   
   /**"version选项：打印程序的版本"*/
   if( options.count( "version" ) ) {
      cout << my->_version << std::endl;
      return false;
   }

   if( options.count( "uuid" ) ) {
      cout << my->_uuid << std::endl;
      return false;
   }
   /**“print-default-config"选项：在cout中打印所有的配置文件信息*/
   if( options.count( "print-default-config" ) ) {
      print_default_config(cout);
      return false;
   }

   /**
    * "data-dir"选项：指定数据文件目录
    * 获取options map中"data-dir"对应的项的内容按照string模板解析成一个智能指针workaround
    * 创建一个boost::path对象，并且用workaround字符串进行实例化
    * 如果调用is_relative函数返回ture，说明时相对路径，则将参数追加当前工作目录的后面
    * 将data_dir的值更新到成员变量_data_dir
    * */
   if( options.count( "data-dir" ) ) {
      // Workaround for 10+ year old Boost defect
      // See https://svn.boost.org/trac10/ticket/8535
      // Should be .as<bfs::path>() but paths with escaped spaces break bpo e.g.
      // std::exception::what: the argument ('/path/with/white\ space') for option '--data-dir' is invalid
      auto workaround = options["data-dir"].as<std::string>();
      bfs::path data_dir = workaround;
      if( data_dir.is_relative() )
         data_dir = bfs::current_path() / data_dir;
      my->_data_dir = data_dir;
   }
   /** "config-dir"选项：指定配置文件目录 */
   if( options.count( "config-dir" ) ) {
      auto workaround = options["config-dir"].as<std::string>();
      bfs::path config_dir = workaround;
      if( config_dir.is_relative() )
         config_dir = bfs::current_path() / config_dir;
      my->_config_dir = config_dir;
   }

   /** 配置文件路径指定完成后更新额日志文件的路径值 */
   auto workaround = options["logconf"].as<std::string>();
   bfs::path logconf = workaround;
   if( logconf.is_relative() )
      logconf = my->_config_dir / logconf;
   my->_logging_conf = logconf;

   /** 配置文件路径指定完成后更新配置文件的路径值 */
   workaround = options["config"].as<std::string>();
   bfs::path config_file_name = workaround;
   if( config_file_name.is_relative() )
      config_file_name = my->_config_dir / config_file_name;

   /**
    * 如果新的日志文件不存在
    *    1、新的日志文件文件名不是config.ini，则报错
    *    2、写入日志文件
   */
   if(!bfs::exists(config_file_name)) {
      if(config_file_name.compare(my->_config_dir / "config.ini") != 0)
      {
         cout << "Config file " << config_file_name << " missing." << std::endl;
         return false;
      }
      write_default_config(config_file_name);
   }
   /**
    * 1、根据配置文件名称调用parse_config_file方法更新类成员配置描述器_cfg_options，注意第二个参数，我们必须传入true，因为
    *    配置文件中有可能不存在_cfg_options中存在的选项，所以我们需要允许配置文件存在未定义的选项，否则这个函数会抛出异常导致
    *    程序结束
    * 2、调用store函数重新解析命令参数，将解析结果存储在options map容器中重新用于后续的命令解析处理
   */
   bpo::store(bpo::parse_config_file<char>(config_file_name.make_preferred().string().c_str(),
                                           my->_cfg_options, true), options);

   /** 
    * plugin选项处理
    * 注意：插件的初始化会在app对象中维护plugin_initialized向量，每初始化成功一个插件就会这个向量中push一个插件对象的指针
   */
   if(options.count("plugin") > 0)
   {
      /** 
       * 1、将options关联容器中plugin项解析成一个string的向量
       * 2、遍历这个向量，将向量中的每一项名称用‘\t’进行拆分，获得新的向量names
       * 3、调用get_plugin方法获取插件的abstract_plugin引用并调用initialize方法进行初始化
       */
      auto plugins = options.at("plugin").as<std::vector<std::string>>();
      
      for(auto& arg : plugins)
      {
         vector<string> names;
         boost::split(names, arg, boost::is_any_of(" \t,"));
         for(const std::string& name : names)
            get_plugin(name).initialize(options);
      }
   }
   /**
    * 自启动插件初始化
    * 由于这些插件写死在了代码中，也就意味着这几个插件时必选插件，所以一定会执行初始化
    * 遍历autostart_plugins对象列表，如果对象迭代器为空(再命令行参数未找到)或者插件已经构建但是为初始化(在配置文件中没有配置该插件但是在app().initialize已经构建)则需要进行插件初始化
    */
   try {
      for (auto plugin : autostart_plugins)
         if (plugin != nullptr && plugin->get_state() == abstract_plugin::registered)
            plugin->initialize(options);
      /** 将解析的结构存储到外部关联容器变量options中 */      
      bpo::notify(options);
   } catch (...) {
      std::cerr << "Failed to initialize\n";
      return false;
   }

   return true;
}
/**
 * @brief 关闭所有运行中的插件
 * @param none
 * @return none
*/
void application::shutdown() {
   /** 
    * 遍历运行的插件，调用每个插件的关闭函数，原理与start方法类似
   */
   for(auto ritr = running_plugins.rbegin();
       ritr != running_plugins.rend(); ++ritr) {
      (*ritr)->shutdown();
   }
   /**
    * 清空运行的插件向量
    * 清空初始化的插件向量
    * 清空注册的插件
    * 复位io_serv
   */
   for(auto ritr = running_plugins.rbegin();
       ritr != running_plugins.rend(); ++ritr) {
      plugins.erase((*ritr)->name());
   }
   running_plugins.clear();
   initialized_plugins.clear();
   plugins.clear();
   io_serv.reset();
}

void application::quit() {
   io_serv->stop();
}

/**
 * @brief   完成特殊信号注册以及回调函数设置，和开启消息循环(io_serv->run())
 * @param   none
 * @pre     需要进行插件注册以及启动
 * @return  none
*/
void application::exec() {
   /**注册SIGINT信号(该信号响应Ctrl+C)，返回一个signal_set对象指针*/
   std::shared_ptr<boost::asio::signal_set> sigint_set(new boost::asio::signal_set(*io_serv, SIGINT));
   /**
    * 注册回调函数
    * TIPS:async_wait函数时注册一个回调函数，回调函数是一个Lambda表达式函数，函数中可以访问sigint_set对象指针和application的this指针(为了访问类对象的quit方法)
    * 以下代码可以写成如下的格式：
    * aoto handler = [sigint_set,this](const boost::system::error_code& err, int num)
    * {
    *    quit();
    *    sigint_set->cancel();
    * }   
    * sigint_set->async_wait(handler);
   */
   sigint_set->async_wait([sigint_set,this](const boost::system::error_code& err, int num) {
     quit();
     sigint_set->cancel();
   });

   /**同理注册SIGTERM和SIGPIPE信号*/
   std::shared_ptr<boost::asio::signal_set> sigterm_set(new boost::asio::signal_set(*io_serv, SIGTERM));
   sigterm_set->async_wait([sigterm_set,this](const boost::system::error_code& err, int num) {
     quit();
     sigterm_set->cancel();
   });

   std::shared_ptr<boost::asio::signal_set> sigpipe_set(new boost::asio::signal_set(*io_serv, SIGPIPE));
   sigpipe_set->async_wait([sigpipe_set,this](const boost::system::error_code& err, int num) {
     quit();
     sigpipe_set->cancel();
   });

   /**启动事件循环，阻塞等待信号事件*/
   io_serv->run();
   /**事件处理完成，退出循环*/
   shutdown(); /// perform synchronous shutdown
}

/**
 * @brief 写入默认的配置文件
 * @param[in] cfg_file   配置文件路径
 * @pre  在指定的配置文件路径下没有配置文件
 * @return none
*/
void application::write_default_config(const bfs::path& cfg_file) {
   /**
    * 如果配置文件的父路径不存在，则创建该付路径
   */
   if(!bfs::exists(cfg_file.parent_path()))
      bfs::create_directories(cfg_file.parent_path());
   /**
    * 调用make_preferred删除冗余当前目录（.），父目录（..）和目录分隔符元素并获取string字符串
    * 创建文件流对象并且调用print_default_config写入配置文件后关闭文件流
   */
   std::ofstream out_cfg( bfs::path(cfg_file).make_preferred().string());
   print_default_config(out_cfg);
   out_cfg.close();
}

/**
 * @brief print_default_config
 * @param[in] os   输出对象的引用，用于输出到设备中，默认使用cout
 * @return none
*/
void application::print_default_config(std::ostream& os) {
   /**
    * 声明一个map对象用于存储插件的长名称和短名称的对应关系
   */
   std::map<std::string, std::string> option_to_plug;
   /*
   * 遍历插件，获取默认的插件配置选项，将获取到默认插件配置选项通过引用别名
   * opt进行访问，并赋值插件名称作为配置的长名称
   */
   for(auto& plug : plugins) {
      boost::program_options::options_description plugin_cli_opts;
      boost::program_options::options_description plugin_cfg_opts;
      plug.second->set_program_options(plugin_cli_opts, plugin_cfg_opts);

      for(const boost::shared_ptr<bpo::option_description>& opt : plugin_cfg_opts.options())
         option_to_plug[opt->long_name()] = plug.second->name();
   }
   /**
    * 通过共享内存指针遍历程序的配置选项
    * 1、如果配置选项的描述存在，则将配置描述的内容赋值给一个字符串，并将该字符串中的所有格式化字符\n替换成\n#
    * 2、如果在在迭代对象中的长名称在option_to_plug中，则在括号中打印出abstract_plugin对象指针的内容
    *   例如：
    *       # this peer will request no pending transactions from other nodes (eosio::bnet_plugin)
    * 3、如果semantic方法返回的共享value_semantic对象指针并调用value_semantic的apply_default方法返回false，
    *    则说明该选项未分配默认值，那么按照# 选项长名称= 格式打印
    *    例如：
    *       # bnet-connect = 
    * 4、如果步骤3返回成功，则说明该选项已经分配了默认值，那么调用format_parameter犯法获取参数名称和属性,返回格式如下：
    *    arg (=0.0.0.0:4321)，然后调用std::string的成员函数erase，擦除前6个字符，即保留等号后面的字符，然后继续擦除
    *    最后一个字符即右括号，得到完整的value值，然后按照long_name = value的格式进行输出，
    *    例如：
    *       bnet-endpoint = 0.0.0.0:4321
   */
   for(const boost::shared_ptr<bpo::option_description> od : my->_cfg_options.options())
   {
      if(!od->description().empty()) {
         std::string desc = od->description();
         boost::replace_all(desc, "\n", "\n# ");
         os << "# " << desc;
         std::map<std::string, std::string>::iterator it;
         if((it = option_to_plug.find(od->long_name())) != option_to_plug.end())
            os << " (" << it->second << ")";
         os << std::endl;
      }
      boost::any store;
      if(!od->semantic()->apply_default(store))
         os << "# " << od->long_name() << " = " << std::endl;
      else {
         auto example = od->format_parameter();
         if(example.empty())
            // This is a boolean switch
            os << od->long_name() << " = " << "false" << std::endl;
         else {
            // The string is formatted "arg (=<interesting part>)"
            example.erase(0, 6);
            example.erase(example.length()-1);
            os << od->long_name() << " = " << example << std::endl;
         }
      }
      os << std::endl;
   }
}

/**
 * @brief 根据插件名查找插件对象指针abstract_plugin
 * @param[in] name   插件名
 * @return abstract_plugin指针
*/
abstract_plugin* application::find_plugin(const string& name)const
{
   auto itr = plugins.find(name);
   if(itr == plugins.end()) {
      return nullptr;
   }
   return itr->second.get();
}

/**
 * @brief 根据插件名获取插件对象引用
 * @param[in] name   插件名
 * @return abstract_plugin引用
*/
abstract_plugin& application::get_plugin(const string& name)const {
   auto ptr = find_plugin(name);
   if(!ptr)
      BOOST_THROW_EXCEPTION(std::runtime_error("unable to find plugin: " + name));
   return *ptr;
}

bfs::path application::data_dir() const {
   return my->_data_dir;
}

bfs::path application::config_dir() const {
   return my->_config_dir;
}

} /// namespace appbase
