/*
 * Copyright (c) 2015 Cryptonomex, Inc., and contributors.
 *
 * The MIT License
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */
#include <graphene/app/application.hpp>
#include <graphene/app/config_util.hpp>

#include <graphene/chain/balance_object.hpp>

#include <graphene/utilities/tempdir.hpp>

#include <fc/thread/thread.hpp>
#include <fc/log/appender.hpp>
#include <fc/log/console_appender.hpp>
#include <fc/log/logger.hpp>
#include <fc/log/logger_config.hpp>

#include <boost/filesystem/path.hpp>

#include "../../libraries/app/application_impl.hxx"

#include "../common/init_unit_test_suite.hpp"
#include "../common/genesis_file_util.hpp"
#include "../common/program_options_util.hpp"
#include "../common/utils.hpp"

using namespace graphene;
namespace bpo = boost::program_options;

namespace fc {
   extern std::unordered_map<std::string, logger> &get_logger_map();
   extern std::unordered_map<std::string, appender::ptr> &get_appender_map();
}

BOOST_AUTO_TEST_CASE(load_configuration_options_test_config_logging_files_created)
{
   fc::temp_directory app_dir(graphene::utilities::temp_directory_path());
   auto dir = app_dir.path();
   auto config_ini_file = dir / "config.ini";
   auto logging_ini_file = dir / "logging.ini";

   /// create default config options
   auto node = new app::application();
   bpo::options_description cli, cfg;
   node->set_program_options(cli, cfg);
   bpo::options_description cfg_options("Esher Witness Node");
   cfg_options.add(cfg);

   /// check preconditions
   BOOST_CHECK(!fc::exists(config_ini_file));
   BOOST_CHECK(!fc::exists(logging_ini_file));

   bpo::variables_map options;
   app::load_configuration_options(dir, cfg_options, options);

   /// check post-conditions
   BOOST_CHECK(fc::exists(config_ini_file));
   BOOST_CHECK(fc::exists(logging_ini_file));
   BOOST_CHECK_GT(fc::file_size(config_ini_file), 0u);
   BOOST_CHECK_GT(fc::file_size(logging_ini_file), 0u);
}

BOOST_AUTO_TEST_CASE(load_configuration_options_test_config_ini_options)
{
   fc::temp_directory app_dir(graphene::utilities::temp_directory_path());
   auto dir = app_dir.path();
   auto config_ini_file = dir / "config.ini";
   auto logging_ini_file = dir / "logging.ini";

   /// create config.ini
   bpo::options_description cfg_options("config.ini options");
   cfg_options.add_options()
   ("option1", bpo::value<std::string>(), "")
   ("option2", bpo::value<int>(), "")
   ;
   std::ofstream out(config_ini_file.preferred_string());
   out << "option1=is present\n"
          "option2=1\n\n";
   out.close();

   /// check preconditions
   BOOST_CHECK(fc::exists(config_ini_file));
   BOOST_CHECK(!fc::exists(logging_ini_file));

   bpo::variables_map options;
   app::load_configuration_options(dir, cfg_options, options);

   /// check the options values are parsed into the output map
   BOOST_CHECK(!options.empty());
   BOOST_CHECK_EQUAL(options.count("option1"), 1u);
   BOOST_CHECK_EQUAL(options.count("option2"), 1u);
   BOOST_CHECK_EQUAL(options["option1"].as<std::string>(), "is present");
   BOOST_CHECK_EQUAL(options["option2"].as<int>(), 1);

   /// when the config.ini exists and doesn't contain logging configuration while the logging.ini doesn't exist
   /// the logging.ini is not created
   BOOST_CHECK(!fc::exists(logging_ini_file));
}

BOOST_AUTO_TEST_CASE(load_configuration_options_test_logging_ini_options)
{
   fc::temp_directory app_dir(graphene::utilities::temp_directory_path());
   auto dir = app_dir.path();
   auto config_ini_file = dir / "config.ini";
   auto logging_ini_file = dir / "logging.ini";

   /// create logging.ini
   /// configure exactly one logger and appender
   std::ofstream out(logging_ini_file.preferred_string());
   out << "[log.file_appender.default]\n"
          "filename=test.log\n\n"
          "[logger.default]\n"
          "level=info\n"
          "appenders=default\n\n"
          ;
   out.close();

   /// clear logger and appender state
   fc::get_logger_map().clear();
   fc::get_appender_map().clear();
   BOOST_CHECK(fc::get_logger_map().empty());
   BOOST_CHECK(fc::get_appender_map().empty());

   bpo::options_description cfg_options("empty");
   bpo::variables_map options;
   app::load_configuration_options(dir, cfg_options, options);

   /// check the options values are parsed into the output map
   /// this is a little bit tricky since load_configuration_options() doesn't provide output variable for logging_config
   auto logger_map = fc::get_logger_map();
   auto appender_map = fc::get_appender_map();
   BOOST_CHECK_EQUAL(logger_map.size(), 1u);
   BOOST_CHECK(logger_map.count("default"));
   BOOST_CHECK_EQUAL(appender_map.size(), 1u);
   BOOST_CHECK(appender_map.count("default"));
}

BOOST_AUTO_TEST_CASE(load_configuration_options_test_legacy_config_ini_options)
{
   fc::temp_directory app_dir(graphene::utilities::temp_directory_path());
   auto dir = app_dir.path();
   auto config_ini_file = dir / "config.ini";
   auto logging_ini_file = dir / "logging.ini";

   /// create config.ini
   bpo::options_description cfg_options("config.ini options");
   cfg_options.add_options()
   ("option1", bpo::value<std::string>(), "")
   ("option2", bpo::value<int>(), "")
   ;
   std::ofstream out(config_ini_file.preferred_string());
   out << "option1=is present\n"
          "option2=1\n\n"
          "[log.file_appender.default]\n"
          "filename=test.log\n\n"
          "[logger.default]\n"
          "level=info\n"
          "appenders=default\n\n"
          ;
   out.close();

   /// clear logger and appender state
   fc::get_logger_map().clear();
   fc::get_appender_map().clear();
   BOOST_CHECK(fc::get_logger_map().empty());
   BOOST_CHECK(fc::get_appender_map().empty());

   bpo::variables_map options;
   app::load_configuration_options(dir, cfg_options, options);

   /// check logging.ini not created
   BOOST_CHECK(!fc::exists(logging_ini_file));

   /// check the options values are parsed into the output map
   BOOST_CHECK(!options.empty());
   BOOST_CHECK_EQUAL(options.count("option1"), 1u);
   BOOST_CHECK_EQUAL(options.count("option2"), 1u);
   BOOST_CHECK_EQUAL(options["option1"].as<std::string>(), "is present");
   BOOST_CHECK_EQUAL(options["option2"].as<int>(), 1);

   auto logger_map = fc::get_logger_map();
   auto appender_map = fc::get_appender_map();
   BOOST_CHECK_EQUAL(logger_map.size(), 1u);
   BOOST_CHECK(logger_map.count("default"));
   BOOST_CHECK_EQUAL(appender_map.size(), 1u);
   BOOST_CHECK(appender_map.count("default"));
}

/////////////
/// @brief create a 3 node network
/////////////
BOOST_AUTO_TEST_CASE( three_node_network )
{
   using namespace graphene::chain;
   using namespace graphene::app;
   try {
      // Configure logging
      fc::logging_config logging_config = fc::logging_config::default_config();

      auto logger = logging_config.loggers.back(); // get a copy of the default logger
      logger.name = "p2p";                         // update the name to p2p
      logging_config.loggers.push_back( logger );  // add it to logging_config

      fc::configure_logging(logging_config);

      // Start app1
      BOOST_TEST_MESSAGE( "Creating and initializing app1" );

      auto port = fc::network::get_available_port();
      auto app1_p2p_endpoint_str = string("127.0.0.1:") + std::to_string(port);
      auto app2_seed_nodes_str = string("[\"") + app1_p2p_endpoint_str + "\"]";
      auto app3_seed_nodes_str = string("[\"") + app1_p2p_endpoint_str + "\"]";

      fc::temp_directory app_dir( graphene::utilities::temp_directory_path() );
      auto genesis_file = create_genesis_file(app_dir);

      graphene::app::application app1;
      auto sharable_cfg = std::make_shared<boost::program_options::variables_map>();
      auto& cfg = *sharable_cfg;
      fc::set_option( cfg, "p2p-endpoint", app1_p2p_endpoint_str );
      fc::set_option( cfg, "genesis-json", genesis_file );
      fc::set_option( cfg, "seed-nodes", string("[]") );
      app1.initialize(app_dir.path(), sharable_cfg);
      BOOST_TEST_MESSAGE( "Starting app1 and waiting" );
      app1.startup();

      auto node_startup_wait_time = fc::seconds(15);

      fc::wait_for( node_startup_wait_time, [&app1,port] () {
         const auto status = app1.p2p_node()->network_get_info();
         return status["listening_on"].as<fc::ip::endpoint>( 5 ).port() == port;
      });

      // Start app2
      BOOST_TEST_MESSAGE( "Creating and initializing app2" );

      fc::temp_directory app2_dir( graphene::utilities::temp_directory_path() );
      graphene::app::application app2;
      auto sharable_cfg2 = std::make_shared<boost::program_options::variables_map>();
      auto& cfg2 = *sharable_cfg2;
      fc::set_option( cfg2, "genesis-json", genesis_file );
      fc::set_option( cfg2, "seed-nodes", app2_seed_nodes_str );
      app2.initialize(app2_dir.path(), sharable_cfg2);

      BOOST_TEST_MESSAGE( "Starting app2 and waiting for connection" );
      app2.startup();

      fc::wait_for( node_startup_wait_time, [&app1] () {
         if( app1.p2p_node()->get_connection_count() > 0 )
         {
            auto peers = app1.p2p_node()->get_connected_peers();
            BOOST_REQUIRE_EQUAL( peers.size(), 1u );
            const auto& peer_info = peers.front().info;
            auto itr = peer_info.find( "peer_needs_sync_items_from_us" );
            if( itr == peer_info.end() )
               return false;
            return !itr->value().as<bool>(1);
         }
         return false;
      });

      BOOST_REQUIRE_EQUAL(app1.p2p_node()->get_connection_count(), 1u);
      BOOST_CHECK_EQUAL(std::string(app1.p2p_node()->get_connected_peers().front().host.get_address()), "127.0.0.1");
      BOOST_TEST_MESSAGE( "app1 and app2 successfully connected" );

      std::shared_ptr<chain::database> db1 = app1.chain_database();
      std::shared_ptr<chain::database> db2 = app2.chain_database();

      BOOST_CHECK_EQUAL( db1->get_balance( GRAPHENE_NULL_ACCOUNT, asset_id_type() ).amount.value, 0 );
      BOOST_CHECK_EQUAL( db2->get_balance( GRAPHENE_NULL_ACCOUNT, asset_id_type() ).amount.value, 0 );

      // Transaction test
      BOOST_TEST_MESSAGE( "Creating transfer tx" );
      graphene::chain::precomputable_transaction trx;
      {
         account_id_type nathan_id = db2->get_index_type<account_index>().indices().get<by_name>().find( "nathan" )
                                        ->get_id();
         fc::ecc::private_key nathan_key = fc::ecc::private_key::regenerate(fc::sha256::hash(string("nathan")));

         balance_claim_operation claim_op;
         balance_id_type bid = balance_id_type();
         claim_op.deposit_to_account = nathan_id;
         claim_op.balance_to_claim = bid;
         claim_op.balance_owner_key = nathan_key.get_public_key();
         claim_op.total_claimed = bid(*db1).balance;
         trx.operations.push_back( claim_op );
         db1->current_fee_schedule().set_fee( trx.operations.back() );

         transfer_operation xfer_op;
         xfer_op.from = nathan_id;
         xfer_op.to = GRAPHENE_NULL_ACCOUNT;
         xfer_op.amount = asset( 1000000 );
         trx.operations.push_back( xfer_op );
         db1->current_fee_schedule().set_fee( trx.operations.back() );

         trx.set_expiration( db1->get_slot_time( 10 ) );
         trx.sign( nathan_key, db1->get_chain_id() );
         trx.validate();
      }

      BOOST_TEST_MESSAGE( "Pushing tx locally on db1" );
      processed_transaction ptrx = db1->push_transaction(trx);

      BOOST_CHECK_EQUAL( db1->get_balance( GRAPHENE_NULL_ACCOUNT, asset_id_type() ).amount.value, 1000000 );
      BOOST_CHECK_EQUAL( db2->get_balance( GRAPHENE_NULL_ACCOUNT, asset_id_type() ).amount.value, 0 );

      BOOST_TEST_MESSAGE( "Broadcasting tx" );
      app1.p2p_node()->broadcast(graphene::net::trx_message(trx));

      auto broadcast_wait_time = fc::seconds(15);

      fc::wait_for( broadcast_wait_time, [db2] () {
         return db2->get_balance( GRAPHENE_NULL_ACCOUNT, asset_id_type() ).amount.value == 1000000;
      });

      BOOST_CHECK_EQUAL( db1->get_balance( GRAPHENE_NULL_ACCOUNT, asset_id_type() ).amount.value, 1000000 );
      BOOST_CHECK_EQUAL( db2->get_balance( GRAPHENE_NULL_ACCOUNT, asset_id_type() ).amount.value, 1000000 );

      // Block test
      BOOST_TEST_MESSAGE( "Generating block on db2" );
      fc::ecc::private_key committee_key = fc::ecc::private_key::regenerate(fc::sha256::hash(string("nathan")));

      // the other node will reject the block if its timestamp is in the future, so we wait
      fc::wait_for( broadcast_wait_time, [db2] () {
         return db2->get_slot_time(1) <= fc::time_point::now();
      });

      auto block_1 = db2->generate_block(
         db2->get_slot_time(1),
         db2->get_scheduled_witness(1),
         committee_key,
         database::skip_nothing);

      BOOST_CHECK_EQUAL( db1->head_block_num(), 0u );
      BOOST_CHECK_EQUAL( db2->head_block_num(), 1u );
      BOOST_CHECK_EQUAL( block_1.block_num(), 1u );

      BOOST_TEST_MESSAGE( "Broadcasting block" );
      app2.p2p_node()->broadcast(graphene::net::block_message( block_1 ));

      fc::wait_for( broadcast_wait_time, [db1] () {
         return db1->head_block_num() == 1;
      });

      BOOST_TEST_MESSAGE( "Verifying nodes are still connected" );
      BOOST_CHECK_EQUAL(app1.p2p_node()->get_connection_count(), 1u);
      BOOST_CHECK_EQUAL(app1.chain_database()->head_block_num(), 1u);

      BOOST_TEST_MESSAGE( "Checking GRAPHENE_NULL_ACCOUNT has balance" );
      BOOST_CHECK_EQUAL( db1->get_balance( GRAPHENE_NULL_ACCOUNT, asset_id_type() ).amount.value, 1000000 );
      BOOST_CHECK_EQUAL( db2->get_balance( GRAPHENE_NULL_ACCOUNT, asset_id_type() ).amount.value, 1000000 );

      // Start app3
      BOOST_TEST_MESSAGE( "Creating and initializing app3" );

      fc::temp_directory app3_dir( graphene::utilities::temp_directory_path() );
      graphene::app::application app3;
      auto sharable_cfg3 = std::make_shared<boost::program_options::variables_map>();
      auto& cfg3 = *sharable_cfg3;
      fc::set_option( cfg3, "genesis-json", genesis_file );
      fc::set_option( cfg3, "seed-nodes", app3_seed_nodes_str );
      fc::set_option( cfg3, "p2p-accept-incoming-connections", false );
      app3.initialize(app3_dir.path(), sharable_cfg3);

      BOOST_TEST_MESSAGE( "Starting app3 and waiting for connection" );
      app3.startup();

      fc::wait_for( node_startup_wait_time, [&app1] () {
         if( app1.p2p_node()->get_connection_count() < 2 )
            return false;
         auto peers = app1.p2p_node()->get_connected_peers();
         if( peers.size() < 2 )
            return false;
         for( const auto& peer : peers )
         {
            auto itr = peer.info.find( "peer_needs_sync_items_from_us" );
            if( itr == peer.info.end() )
               return false;
            if( itr->value().as<bool>(1) )
               return false;
         }
         return true;
      });

      BOOST_REQUIRE_EQUAL(app1.p2p_node()->get_connection_count(), 2u);
      BOOST_TEST_MESSAGE( "app1 and app3 successfully connected" );

      BOOST_TEST_MESSAGE( "Verifying app3 is synced" );
      BOOST_CHECK_EQUAL( app3.chain_database()->head_block_num(), 1u);
      BOOST_CHECK_EQUAL( app3.chain_database()->get_balance( GRAPHENE_NULL_ACCOUNT, asset_id_type() ).amount.value,
                         1000000 );

      auto new_peer_wait_time = fc::seconds(45);

      BOOST_TEST_MESSAGE( "Waiting for app2 and app3 to connect to each other" );
      fc::wait_for( new_peer_wait_time, [&app2] () {
         if( app2.p2p_node()->get_connection_count() < 2 )
            return false;
         auto peers = app2.p2p_node()->get_connected_peers();
         if( peers.size() < 2 )
            return false;
         for( const auto& peer : peers )
         {
            auto itr = peer.info.find( "peer_needs_sync_items_from_us" );
            if( itr == peer.info.end() )
               return false;
            if( itr->value().as<bool>(1) )
               return false;
         }
         return true;
      });

      BOOST_REQUIRE_EQUAL(app3.p2p_node()->get_connection_count(), 2u);
      BOOST_TEST_MESSAGE( "app2 and app3 successfully connected" );

   } catch( fc::exception& e ) {
      edump((e.to_detail_string()));
      throw;
   }
}

/// a contrived example to test the breaking out of application_impl to a header file
BOOST_AUTO_TEST_CASE(application_impl_breakout) {

   static graphene::app::application my_app;

   class test_impl : public graphene::app::detail::application_impl {
      // override the constructor, just to test that we can
   public:
      test_impl() : application_impl(my_app) {}
      bool has_item(const net::item_id& id) override {
         return true;
      }
   };

   test_impl impl;
   graphene::net::item_id id;
   BOOST_CHECK(impl.has_item(id));
}
