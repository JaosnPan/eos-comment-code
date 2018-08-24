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
 * @brief ������ʼ���Ĳ��
 * @param none
 * @return none
*/
void application::startup() {
   /**
    * ����initialized_plugins���Ա������ÿ�������startup����
    * ע�⣺ÿ�������startup�̳���pluginģ����(application.hpp)��plugin��̳���abstract_plugin��(plugin.hpp),plugin����д��startup�麯���������п���ĳЩ
    * ���û���Զ��幦�ܣ�����Ҳû����д��startup,ֱ��ִ��plugin�����������
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
 * @brief ����ע������Ĭ�ϵĳ��������в�����������ѡ��
 * @param none
 * @return none
*/
void application::set_program_options()
{
   /**�����Աpluginsʹ�ñ���plug����ѭ�����ʣ�����ÿһ�������ѡ������
    * ����c++11��һ�����ԣ�ע������&��&��ζ�߿��Ըı��Ӧ��ֵ��forѭ���е�ÿ��������ʼ��һ���µ�����
    * for (auto it = plug.begin(); it != plug.end(); ++it)
    * {
    *    auto &plug = *it;
    * }
    */
   for(auto& plug : plugins) {
      /**
       * 1����������ѡ����������
       *    �����в���������
       *    ����ѡ��������
       * 2�����ò����ʵ�����set_program_options�����������ó���ѡ�ע��ÿһ�������application��plugin��ģ�壬plugin�̳���abstract_plugin��
       * abstract_plugin��������set_program_options�麯���������п���ĳ�����������δ��д������������Զ��ڲ�ͬ�Ĳ������û�����ò���ѡ����������в���
       * ѡ��
       * 3���������Ĳ������д��set_program_options��������������������Ҫ��application��ĳ�Ա�����в��������ò��������������в�����
       * 4��ע��unique_ptrָ��my�����������ʱc++11�����ԣ����ڽ�ѡ�������������ƽ���my��Ӧ�Ķ���
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
    * 1����������ѡ��������
    *    Ӧ�õ������в���ѡ��
    *    Ӧ�õ����ò���ѡ��
    * 2�����������в����Լ����ò�������һ��������Ӧ�����������в�����Ӧ�����ò��������ƽ���my��Ӧ�Ķ���
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
    * @brief �����ʼ��ʵ�ֺ���
    * @param[in] argc   ��������
    * @param[in] argv   �����б�
    * @param[in] autostart_plugins  �������������ָ������
    * @return true/false
    */
bool application::initialize_impl(int argc, char** argv, vector<abstract_plugin*> autostart_plugins) {
   set_program_options(); /**����ע������Ĭ�ϵĳ��������в�����������ѡ��*/

   /**
    * 1������һ��ѡ��洢map����
    * 2�����ϲ㴫��������в������н���������������洢��options������
   */
   bpo::variables_map options;
   bpo::store(bpo::parse_command_line(argc, argv, my->_app_options), options);

   /**
    * ������ɣ����ݶ�Ӧ�Ĳ������д���
   */

   /**helpѡ���ӡ_app_options�е���������*/
   if( options.count( "help" ) ) {
      cout << my->_app_options << std::endl;
      return false;
   }
   
   /**"versionѡ���ӡ����İ汾"*/
   if( options.count( "version" ) ) {
      cout << my->_version << std::endl;
      return false;
   }

   if( options.count( "uuid" ) ) {
      cout << my->_uuid << std::endl;
      return false;
   }
   /**��print-default-config"ѡ���cout�д�ӡ���е������ļ���Ϣ*/
   if( options.count( "print-default-config" ) ) {
      print_default_config(cout);
      return false;
   }

   /**
    * "data-dir"ѡ�ָ�������ļ�Ŀ¼
    * ��ȡoptions map��"data-dir"��Ӧ��������ݰ���stringģ�������һ������ָ��workaround
    * ����һ��boost::path���󣬲�����workaround�ַ�������ʵ����
    * �������is_relative��������ture��˵��ʱ���·�����򽫲���׷�ӵ�ǰ����Ŀ¼�ĺ���
    * ��data_dir��ֵ���µ���Ա����_data_dir
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
   /** "config-dir"ѡ�ָ�������ļ�Ŀ¼ */
   if( options.count( "config-dir" ) ) {
      auto workaround = options["config-dir"].as<std::string>();
      bfs::path config_dir = workaround;
      if( config_dir.is_relative() )
         config_dir = bfs::current_path() / config_dir;
      my->_config_dir = config_dir;
   }

   /** �����ļ�·��ָ����ɺ���¶���־�ļ���·��ֵ */
   auto workaround = options["logconf"].as<std::string>();
   bfs::path logconf = workaround;
   if( logconf.is_relative() )
      logconf = my->_config_dir / logconf;
   my->_logging_conf = logconf;

   /** �����ļ�·��ָ����ɺ���������ļ���·��ֵ */
   workaround = options["config"].as<std::string>();
   bfs::path config_file_name = workaround;
   if( config_file_name.is_relative() )
      config_file_name = my->_config_dir / config_file_name;

   /**
    * ����µ���־�ļ�������
    *    1���µ���־�ļ��ļ�������config.ini���򱨴�
    *    2��д����־�ļ�
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
    * 1�����������ļ����Ƶ���parse_config_file�����������Ա����������_cfg_options��ע��ڶ������������Ǳ��봫��true����Ϊ
    *    �����ļ����п��ܲ�����_cfg_options�д��ڵ�ѡ�����������Ҫ���������ļ�����δ�����ѡ���������������׳��쳣����
    *    �������
    * 2������store�������½����������������������洢��options map�������������ں����������������
   */
   bpo::store(bpo::parse_config_file<char>(config_file_name.make_preferred().string().c_str(),
                                           my->_cfg_options, true), options);

   /** 
    * pluginѡ���
    * ע�⣺����ĳ�ʼ������app������ά��plugin_initialized������ÿ��ʼ���ɹ�һ������ͻ����������pushһ����������ָ��
   */
   if(options.count("plugin") > 0)
   {
      /** 
       * 1����options����������plugin�������һ��string������
       * 2����������������������е�ÿһ�������á�\t�����в�֣�����µ�����names
       * 3������get_plugin������ȡ�����abstract_plugin���ò�����initialize�������г�ʼ��
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
    * �����������ʼ��
    * ������Щ���д�����˴����У�Ҳ����ζ���⼸�����ʱ��ѡ���������һ����ִ�г�ʼ��
    * ����autostart_plugins�����б�������������Ϊ��(�������в���δ�ҵ�)���߲���Ѿ���������Ϊ��ʼ��(�������ļ���û�����øò��������app().initialize�Ѿ�����)����Ҫ���в����ʼ��
    */
   try {
      for (auto plugin : autostart_plugins)
         if (plugin != nullptr && plugin->get_state() == abstract_plugin::registered)
            plugin->initialize(options);
      /** �������Ľṹ�洢���ⲿ������������options�� */      
      bpo::notify(options);
   } catch (...) {
      std::cerr << "Failed to initialize\n";
      return false;
   }

   return true;
}
/**
 * @brief �ر����������еĲ��
 * @param none
 * @return none
*/
void application::shutdown() {
   /** 
    * �������еĲ��������ÿ������Ĺرպ�����ԭ����start��������
   */
   for(auto ritr = running_plugins.rbegin();
       ritr != running_plugins.rend(); ++ritr) {
      (*ritr)->shutdown();
   }
   /**
    * ������еĲ������
    * ��ճ�ʼ���Ĳ������
    * ���ע��Ĳ��
    * ��λio_serv
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
 * @brief   ��������ź�ע���Լ��ص��������ã��Ϳ�����Ϣѭ��(io_serv->run())
 * @param   none
 * @pre     ��Ҫ���в��ע���Լ�����
 * @return  none
*/
void application::exec() {
   /**ע��SIGINT�ź�(���ź���ӦCtrl+C)������һ��signal_set����ָ��*/
   std::shared_ptr<boost::asio::signal_set> sigint_set(new boost::asio::signal_set(*io_serv, SIGINT));
   /**
    * ע��ص�����
    * TIPS:async_wait����ʱע��һ���ص��������ص�������һ��Lambda���ʽ�����������п��Է���sigint_set����ָ���application��thisָ��(Ϊ�˷���������quit����)
    * ���´������д�����µĸ�ʽ��
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

   /**ͬ��ע��SIGTERM��SIGPIPE�ź�*/
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

   /**�����¼�ѭ���������ȴ��ź��¼�*/
   io_serv->run();
   /**�¼�������ɣ��˳�ѭ��*/
   shutdown(); /// perform synchronous shutdown
}

/**
 * @brief д��Ĭ�ϵ������ļ�
 * @param[in] cfg_file   �����ļ�·��
 * @pre  ��ָ���������ļ�·����û�������ļ�
 * @return none
*/
void application::write_default_config(const bfs::path& cfg_file) {
   /**
    * ��������ļ��ĸ�·�������ڣ��򴴽��ø�·��
   */
   if(!bfs::exists(cfg_file.parent_path()))
      bfs::create_directories(cfg_file.parent_path());
   /**
    * ����make_preferredɾ�����൱ǰĿ¼��.������Ŀ¼��..����Ŀ¼�ָ���Ԫ�ز���ȡstring�ַ���
    * �����ļ��������ҵ���print_default_configд�������ļ���ر��ļ���
   */
   std::ofstream out_cfg( bfs::path(cfg_file).make_preferred().string());
   print_default_config(out_cfg);
   out_cfg.close();
}

/**
 * @brief print_default_config
 * @param[in] os   �����������ã�����������豸�У�Ĭ��ʹ��cout
 * @return none
*/
void application::print_default_config(std::ostream& os) {
   /**
    * ����һ��map�������ڴ洢����ĳ����ƺͶ����ƵĶ�Ӧ��ϵ
   */
   std::map<std::string, std::string> option_to_plug;
   /*
   * �����������ȡĬ�ϵĲ������ѡ�����ȡ��Ĭ�ϲ������ѡ��ͨ�����ñ���
   * opt���з��ʣ�����ֵ���������Ϊ���õĳ�����
   */
   for(auto& plug : plugins) {
      boost::program_options::options_description plugin_cli_opts;
      boost::program_options::options_description plugin_cfg_opts;
      plug.second->set_program_options(plugin_cli_opts, plugin_cfg_opts);

      for(const boost::shared_ptr<bpo::option_description>& opt : plugin_cfg_opts.options())
         option_to_plug[opt->long_name()] = plug.second->name();
   }
   /**
    * ͨ�������ڴ�ָ��������������ѡ��
    * 1���������ѡ����������ڣ����������������ݸ�ֵ��һ���ַ������������ַ����е����и�ʽ���ַ�\n�滻��\n#
    * 2��������ڵ��������еĳ�������option_to_plug�У����������д�ӡ��abstract_plugin����ָ�������
    *   ���磺
    *       # this peer will request no pending transactions from other nodes (eosio::bnet_plugin)
    * 3�����semantic�������صĹ���value_semantic����ָ�벢����value_semantic��apply_default��������false��
    *    ��˵����ѡ��δ����Ĭ��ֵ����ô����# ѡ�����= ��ʽ��ӡ
    *    ���磺
    *       # bnet-connect = 
    * 4���������3���سɹ�����˵����ѡ���Ѿ�������Ĭ��ֵ����ô����format_parameter������ȡ�������ƺ�����,���ظ�ʽ���£�
    *    arg (=0.0.0.0:4321)��Ȼ�����std::string�ĳ�Ա����erase������ǰ6���ַ����������Ⱥź�����ַ���Ȼ���������
    *    ���һ���ַ��������ţ��õ�������valueֵ��Ȼ����long_name = value�ĸ�ʽ���������
    *    ���磺
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
 * @brief ���ݲ�������Ҳ������ָ��abstract_plugin
 * @param[in] name   �����
 * @return abstract_pluginָ��
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
 * @brief ���ݲ������ȡ�����������
 * @param[in] name   �����
 * @return abstract_plugin����
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
