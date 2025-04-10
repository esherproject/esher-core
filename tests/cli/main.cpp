/*
 * Copyright (c) 2018 John Jones, and contributors.
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
#include <graphene/app/plugin.hpp>

#include <graphene/utilities/key_conversion.hpp>
#include <graphene/utilities/tempdir.hpp>

#include <graphene/account_history/account_history_plugin.hpp>
#include <graphene/api_helper_indexes/api_helper_indexes.hpp>
#include <graphene/market_history/market_history_plugin.hpp>
#include <graphene/custom_operations/custom_operations_plugin.hpp>
#include <graphene/egenesis/egenesis.hpp>
#include <graphene/wallet/wallet.hpp>
#include <graphene/chain/hardfork.hpp>

#include <fc/thread/thread.hpp>
#include <fc/network/http/websocket.hpp>
#include <fc/rpc/websocket_api.hpp>
#include <fc/rpc/cli.hpp>
#include <fc/crypto/base58.hpp>
#include <fc/crypto/hex.hpp>

#include <fc/crypto/aes.hpp>

#include <thread>

#include <boost/filesystem/path.hpp>

#include "../common/init_unit_test_suite.hpp"
#include "../common/genesis_file_util.hpp"
#include "../common/program_options_util.hpp"
#include "../common/utils.hpp"

#ifdef _WIN32
/*****
 * Global Initialization for Windows
 * ( sets up Winsock stuf )
 */
int sockInit(void)
{
   WSADATA wsa_data;
   return WSAStartup(MAKEWORD(1,1), &wsa_data);
}
int sockQuit(void)
{
   return WSACleanup();
}
#endif

/*********************
 * Helper Methods
 *********************/

using std::exception;
using std::cerr;

#define INVOKE(test) ((struct test*)this)->test_method();

///////////
/// @brief Start the application
/// @param app_dir the temporary directory to use
/// @param server_port_number to be filled with the rpc endpoint port number
/// @returns the application object
//////////
std::shared_ptr<graphene::app::application> start_application(fc::temp_directory& app_dir, int& server_port_number) {
   auto app1 = std::make_shared<graphene::app::application>();

   app1->register_plugin<graphene::account_history::account_history_plugin>(true);
   app1->register_plugin< graphene::market_history::market_history_plugin >(true);
   app1->register_plugin< graphene::grouped_orders::grouped_orders_plugin>(true);
   app1->register_plugin< graphene::api_helper_indexes::api_helper_indexes>(true);
   app1->register_plugin<graphene::custom_operations::custom_operations_plugin>(true);

   auto sharable_cfg = std::make_shared<boost::program_options::variables_map>();
   auto& cfg = *sharable_cfg;
   server_port_number = fc::network::get_available_port();
   auto p2p_port = server_port_number;
   for( size_t i = 0; i < 10 && p2p_port == server_port_number; ++i )
   {
      p2p_port = fc::network::get_available_port();
   }
   BOOST_REQUIRE( p2p_port != server_port_number );
   fc::set_option( cfg, "rpc-endpoint", string("127.0.0.1:") + std::to_string(server_port_number) );
   fc::set_option( cfg, "p2p-endpoint", string("0.0.0.0:") + std::to_string(p2p_port) );
   fc::set_option( cfg, "genesis-json", create_genesis_file(app_dir) );
   fc::set_option( cfg, "seed-nodes", string("[]") );
   fc::set_option( cfg, "custom-operations-start-block", uint32_t(1) );
   app1->initialize(app_dir.path(), sharable_cfg);

   app1->startup();

   return app1;
}

///////////
/// Send a block to the db
/// @param app the application
/// @param returned_block the signed block
/// @returns true on success
///////////
bool generate_block(std::shared_ptr<graphene::app::application> app, graphene::chain::signed_block& returned_block)
{
   try {
      fc::ecc::private_key committee_key = fc::ecc::private_key::regenerate(fc::sha256::hash(string("nathan")));
      auto db = app->chain_database();
      returned_block = db->generate_block( db->get_slot_time(1),
                                         db->get_scheduled_witness(1),
                                         committee_key,
                                         database::skip_nothing );
      return true;
   } catch (exception &e) {
      return false;
   }
}

bool generate_block(std::shared_ptr<graphene::app::application> app)
{
   graphene::chain::signed_block returned_block;
   return generate_block(app, returned_block);
}


signed_block generate_block(std::shared_ptr<graphene::app::application> app, uint32_t skip, const fc::ecc::private_key& key, int miss_blocks)
{
   // skip == ~0 will skip checks specified in database::validation_steps
   skip |= database::skip_undo_history_check;

   auto db = app->chain_database();
   auto block = db->generate_block(db->get_slot_time(miss_blocks + 1),
                                   db->get_scheduled_witness(miss_blocks + 1),
                                   key, skip);
   db->clear_pending();
   return block;
}


//////
// Generate blocks until the timestamp
//////
uint32_t generate_blocks(std::shared_ptr<graphene::app::application> app, fc::time_point_sec timestamp)
{
   fc::ecc::private_key committee_key = fc::ecc::private_key::regenerate(fc::sha256::hash(string("nathan")));
   uint32_t skip = ~0;
   auto db = app->chain_database();

   generate_block(app);
   auto slots_to_miss = db->get_slot_at_time(timestamp);
   if( slots_to_miss <= 1 )
      return 1;
   --slots_to_miss;
   generate_block(app, skip, committee_key, slots_to_miss);
   return 2;
}


///////////
/// @brief Skip intermediate blocks, and generate a maintenance block
/// @param app the application
/// @returns true on success
///////////
bool generate_maintenance_block(std::shared_ptr<graphene::app::application> app) {
   try {
      fc::ecc::private_key committee_key = fc::ecc::private_key::regenerate(fc::sha256::hash(string("nathan")));
      uint32_t skip = ~0;
      auto db = app->chain_database();
      auto maint_time = db->get_dynamic_global_properties().next_maintenance_time;
      auto slots_to_miss = db->get_slot_at_time(maint_time);
      db->generate_block(db->get_slot_time(slots_to_miss),
            db->get_scheduled_witness(slots_to_miss),
            committee_key,
            skip);
      return true;
   } catch (exception& e)
   {
      return false;
   }
}

///////////
/// Check if hardfork core-2262 has passed
///////////
bool is_hf2262_passed(std::shared_ptr<graphene::app::application> app) {
   auto db = app->chain_database();
   auto maint_time = db->get_dynamic_global_properties().next_maintenance_time;
   return HARDFORK_CORE_2262_PASSED( maint_time );
}

///////////
/// @brief a class to make connecting to the application server easier
///////////
class client_connection
{
public:
   /////////
   // constructor
   /////////
   client_connection(
      std::shared_ptr<graphene::app::application> app,
      const fc::temp_directory& data_dir,
      const int server_port_number,
      const std::string custom_wallet_filename = "wallet.json"
   )
   {
      wallet_data.chain_id = app->chain_database()->get_chain_id();
      wallet_data.ws_server = "ws://127.0.0.1:" + std::to_string(server_port_number);
      wallet_data.ws_user = "";
      wallet_data.ws_password = "";
      websocket_connection  = websocket_client.connect( wallet_data.ws_server );

      api_connection = std::make_shared<fc::rpc::websocket_api_connection>( websocket_connection,
                                                                            GRAPHENE_MAX_NESTED_OBJECTS );

      remote_login_api = api_connection->get_remote_api< graphene::app::login_api >(1);
      BOOST_CHECK(remote_login_api->login( wallet_data.ws_user, wallet_data.ws_password ) );

      wallet_api_ptr = std::make_shared<graphene::wallet::wallet_api>(wallet_data, remote_login_api);
      wallet_filename = data_dir.path().generic_string() + "/" + custom_wallet_filename;
      wallet_api_ptr->set_wallet_filename(wallet_filename);

      wallet_api = fc::api<graphene::wallet::wallet_api>(wallet_api_ptr);

      wallet_cli = std::make_shared<fc::rpc::cli>(GRAPHENE_MAX_NESTED_OBJECTS);
      for( auto& name_formatter : wallet_api_ptr->get_result_formatters() )
         wallet_cli->format_result( name_formatter.first, name_formatter.second );
   }
   ~client_connection()
   {
      wallet_cli->stop();
   }
public:
   fc::http::websocket_client websocket_client;
   graphene::wallet::wallet_data wallet_data;
   fc::http::websocket_connection_ptr websocket_connection;
   std::shared_ptr<fc::rpc::websocket_api_connection> api_connection;
   fc::api<login_api> remote_login_api;
   std::shared_ptr<graphene::wallet::wallet_api> wallet_api_ptr;
   fc::api<graphene::wallet::wallet_api> wallet_api;
   std::shared_ptr<fc::rpc::cli> wallet_cli;
   std::string wallet_filename;
};

///////////////////////////////
// Cli Wallet Fixture
///////////////////////////////

struct cli_fixture
{
#ifdef _WIN32
   struct socket_maintainer {
      socket_maintainer() {
         sockInit();
      }
      ~socket_maintainer() {
         sockQuit();
      }
   } sock_maintainer;
#endif
   int server_port_number;
   fc::temp_directory app_dir;
   std::shared_ptr<graphene::app::application> app1;
   client_connection con;
   std::vector<std::string> nathan_keys;

   cli_fixture() :
      server_port_number(0),
      app_dir( graphene::utilities::temp_directory_path() ),
      app1( start_application(app_dir, server_port_number) ),
      con( app1, app_dir, server_port_number ),
      nathan_keys( {"5KQwrPbwdL6PhXujxW37FSSQZ1JiwsST4cqQzDeyXtP79zkvFD3"} )
   {
      BOOST_TEST_MESSAGE("Setup cli_wallet::boost_fixture_test_case");

      using namespace graphene::chain;
      using namespace graphene::app;

      try
      {
         BOOST_TEST_MESSAGE("Setting wallet password");
         con.wallet_api_ptr->set_password("supersecret");
         con.wallet_api_ptr->unlock("supersecret");

         // import Nathan account
         BOOST_TEST_MESSAGE("Importing nathan key");
         BOOST_CHECK_EQUAL(nathan_keys[0], "5KQwrPbwdL6PhXujxW37FSSQZ1JiwsST4cqQzDeyXtP79zkvFD3");
         BOOST_CHECK(con.wallet_api_ptr->import_key("nathan", nathan_keys[0]));
      } catch( fc::exception& e ) {
         edump((e.to_detail_string()));
         throw;
      }
   }

   ~cli_fixture()
   {
      BOOST_TEST_MESSAGE("Cleanup cli_wallet::boost_fixture_test_case");
   }
};

///////////////////////////////
// Tests
///////////////////////////////

////////////////
// Start a server and connect using the same calls as the CLI
////////////////
BOOST_FIXTURE_TEST_CASE( cli_connect, cli_fixture )
{
   BOOST_TEST_MESSAGE("Testing wallet connection.");
}

////////////////
// Start a server and connect using the same calls as the CLI
// Quit wallet and be sure that file was saved correctly
////////////////
BOOST_FIXTURE_TEST_CASE( cli_quit, cli_fixture )
{
   BOOST_TEST_MESSAGE("Testing wallet connection and quit command.");
   BOOST_CHECK_THROW( con.wallet_api_ptr->quit(), fc::canceled_exception );
}

BOOST_FIXTURE_TEST_CASE( cli_help_gethelp, cli_fixture )
{
   BOOST_TEST_MESSAGE("Testing help and gethelp commands.");
   auto formatters = con.wallet_api_ptr->get_result_formatters();

   string result = con.wallet_api_ptr->help();
   BOOST_CHECK( result.find("gethelp") != string::npos );
   if( formatters.find("help") != formatters.end() )
   {
      BOOST_TEST_MESSAGE("Testing formatter of help");
      string output = formatters["help"](fc::variant(result), fc::variants());
      BOOST_CHECK( output.find("gethelp") != string::npos );
   }

   result = con.wallet_api_ptr->gethelp( "transfer" );
   BOOST_CHECK( result.find("usage") != string::npos );
   if( formatters.find("gethelp") != formatters.end() )
   {
      BOOST_TEST_MESSAGE("Testing formatter of gethelp");
      string output = formatters["gethelp"](fc::variant(result), fc::variants());
      BOOST_CHECK( output.find("usage") != string::npos );
   }
}

BOOST_FIXTURE_TEST_CASE( upgrade_nathan_account, cli_fixture )
{
   try
   {
      BOOST_TEST_MESSAGE("Upgrade Nathan's account");

      account_object nathan_acct_before_upgrade, nathan_acct_after_upgrade;
      std::vector<signed_transaction> import_txs;
      signed_transaction upgrade_tx;

      BOOST_TEST_MESSAGE("Importing nathan's balance");
      import_txs = con.wallet_api_ptr->import_balance("nathan", nathan_keys, true);
      nathan_acct_before_upgrade = con.wallet_api_ptr->get_account("nathan");

      // upgrade nathan
      BOOST_TEST_MESSAGE("Upgrading Nathan to LTM");
      upgrade_tx = con.wallet_api_ptr->upgrade_account("nathan", true);
      nathan_acct_after_upgrade = con.wallet_api_ptr->get_account("nathan");

      // verify that the upgrade was successful
      BOOST_CHECK_PREDICATE(
         std::not_equal_to<uint32_t>(),
         (nathan_acct_before_upgrade.membership_expiration_date.sec_since_epoch())
         (nathan_acct_after_upgrade.membership_expiration_date.sec_since_epoch())
      );
      BOOST_CHECK(nathan_acct_after_upgrade.is_lifetime_member());
   } catch( fc::exception& e ) {
      edump((e.to_detail_string()));
      throw;
   }
}

BOOST_FIXTURE_TEST_CASE( create_new_account, cli_fixture )
{
   try
   {
      INVOKE(upgrade_nathan_account);

      // create a new account
      graphene::wallet::brain_key_info bki = con.wallet_api_ptr->suggest_brain_key();
      BOOST_CHECK(!bki.brain_priv_key.empty());
      signed_transaction create_acct_tx = con.wallet_api_ptr->create_account_with_brain_key(
         bki.brain_priv_key, "jmjatlanta", "nathan", "nathan", true
      );
      // save the private key for this new account in the wallet file
      BOOST_CHECK(con.wallet_api_ptr->import_key("jmjatlanta", bki.wif_priv_key));
      con.wallet_api_ptr->save_wallet_file(con.wallet_filename);

      // attempt to give jmjatlanta some esher
      BOOST_TEST_MESSAGE("Transferring esher from Nathan to jmjatlanta");
      signed_transaction transfer_tx = con.wallet_api_ptr->transfer(
         "nathan", "jmjatlanta", "10000", "1.3.0", "Here are some CORE token for your new account", true
      );
   } catch( fc::exception& e ) {
      edump((e.to_detail_string()));
      throw;
   }
}

BOOST_FIXTURE_TEST_CASE( uia_tests, cli_fixture )
{
   try
   {
      BOOST_TEST_MESSAGE("Cli UIA Tests");

      INVOKE(upgrade_nathan_account);

      BOOST_CHECK(generate_block(app1));

      account_object nathan_acct = con.wallet_api_ptr->get_account("nathan");

      auto formatters = con.wallet_api_ptr->get_result_formatters();

      auto check_account_last_history = [&]( string account, string keyword ) {
         auto history = con.wallet_api_ptr->get_relative_account_history(account, 0, 1, 0);
         BOOST_REQUIRE_GT( history.size(), 0 );
         BOOST_CHECK( history[0].description.find( keyword ) != string::npos );
      };
      auto check_nathan_last_history = [&]( string keyword ) {
         check_account_last_history( "nathan", keyword );
      };

      check_nathan_last_history( "account_upgrade_operation" );

      // Create new asset called BOBCOIN
      {
         BOOST_TEST_MESSAGE("Create UIA 'BOBCOIN'");
         graphene::chain::asset_options asset_ops;
         asset_ops.issuer_permissions = DEFAULT_UIA_ASSET_ISSUER_PERMISSION;
         asset_ops.flags = charge_market_fee | override_authority;
         asset_ops.max_supply = 1000000;
         asset_ops.core_exchange_rate = price(asset(2),asset(1,asset_id_type(1)));
         auto result = con.wallet_api_ptr->create_asset("nathan", "BOBCOIN", 4, asset_ops, {}, true);
         if( formatters.find("create_asset") != formatters.end() )
         {
            BOOST_TEST_MESSAGE("Testing formatter of create_asset");
            string output = formatters["create_asset"](
                  fc::variant(result, FC_PACK_MAX_DEPTH), fc::variants());
            BOOST_CHECK( output.find("BOBCOIN") != string::npos );
         }

         BOOST_CHECK_THROW( con.wallet_api_ptr->get_asset_name("BOBCOI"), fc::exception );
         BOOST_CHECK_EQUAL( con.wallet_api_ptr->get_asset_name("BOBCOIN"), "BOBCOIN" );
         BOOST_CHECK_EQUAL( con.wallet_api_ptr->get_asset_symbol("BOBCOIN"), "BOBCOIN" );

         BOOST_CHECK_THROW( con.wallet_api_ptr->get_account_name("nath"), fc::exception );
         BOOST_CHECK_EQUAL( con.wallet_api_ptr->get_account_name("nathan"), "nathan" );
         BOOST_CHECK( con.wallet_api_ptr->get_account_id("nathan") == con.wallet_api_ptr->get_account("nathan").id );
      }
      BOOST_CHECK(generate_block(app1));

      check_nathan_last_history( "Create User-Issue Asset" );
      check_nathan_last_history( "BOBCOIN" );

      auto bobcoin = con.wallet_api_ptr->get_asset("BOBCOIN");

      BOOST_CHECK( con.wallet_api_ptr->get_asset_id("BOBCOIN") == bobcoin.id );

      bool balance_formatter_tested = false;
      auto check_bobcoin_balance = [&](string account, int64_t amount) {
         auto balances = con.wallet_api_ptr->list_account_balances( account );
         size_t count = 0;
         for( auto& bal : balances )
         {
            if( bal.asset_id == bobcoin.id )
            {
               ++count;
               BOOST_CHECK_EQUAL( bal.amount.value, amount );
            }
         }
         BOOST_CHECK_EQUAL(count, 1u);

         // Testing result formatter
         if( !balance_formatter_tested && formatters.find("list_account_balances") != formatters.end() )
         {
            BOOST_TEST_MESSAGE("Testing formatter of list_account_balances");
            string output = formatters["list_account_balances"](
                  fc::variant(balances, FC_PACK_MAX_DEPTH ), fc::variants());
            BOOST_CHECK( output.find("BOBCOIN") != string::npos );
            balance_formatter_tested = true;
         }
      };
      auto check_nathan_bobcoin_balance = [&](int64_t amount) {
         check_bobcoin_balance( "nathan", amount );
      };

      {
         // Issue asset
         BOOST_TEST_MESSAGE("Issue asset");
         con.wallet_api_ptr->issue_asset("init0", "3", "BOBCOIN", "new coin for you", true);
      }
      BOOST_CHECK(generate_block(app1));

      check_nathan_last_history( "nathan issue 3 BOBCOIN to init0" );
      check_nathan_last_history( "new coin for you" );
      check_account_last_history( "init0", "nathan issue 3 BOBCOIN to init0" );
      check_account_last_history( "init0", "new coin for you" );

      check_bobcoin_balance( "init0", 30000 );

      {
         // Override transfer, and test sign_memo and read_memo by the way
         BOOST_TEST_MESSAGE("Override-transfer BOBCOIN from init0");
         auto handle = con.wallet_api_ptr->begin_builder_transaction();
         override_transfer_operation op;
         op.issuer = con.wallet_api_ptr->get_account( "nathan" ).id;
         op.from = con.wallet_api_ptr->get_account( "init0" ).id;
         op.to = con.wallet_api_ptr->get_account( "nathan" ).id;
         op.amount = bobcoin.amount(10000);

         const auto test_bki = con.wallet_api_ptr->suggest_brain_key();
         auto test_pubkey = fc::json::to_string( test_bki.pub_key );
         test_pubkey = test_pubkey.substr( 1, test_pubkey.size() - 2 );
         idump( (test_pubkey) );
         op.memo = con.wallet_api_ptr->sign_memo( "nathan", test_pubkey, "get back some coin" );
         idump( (op.memo) );
         con.wallet_api_ptr->add_operation_to_builder_transaction( handle, op );
         con.wallet_api_ptr->set_fees_on_builder_transaction( handle, "1.3.0" );
         con.wallet_api_ptr->sign_builder_transaction( handle, true );

         auto memo = con.wallet_api_ptr->read_memo( *op.memo );
         BOOST_CHECK_EQUAL( memo, "get back some coin" );

         op.memo = con.wallet_api_ptr->sign_memo( test_pubkey, "nathan", "another test" );
         idump( (op.memo) );
         memo = con.wallet_api_ptr->read_memo( *op.memo );
         BOOST_CHECK_EQUAL( memo, "another test" );

         BOOST_CHECK_THROW( con.wallet_api_ptr->sign_memo( "non-exist-account-or-label", "nathan", "some text" ),
                            fc::exception );
         BOOST_CHECK_THROW( con.wallet_api_ptr->sign_memo( "nathan", "non-exist-account-or-label", "some text" ),
                            fc::exception );
      }
      BOOST_CHECK(generate_block(app1));

      check_nathan_last_history( "nathan force-transfer 1 BOBCOIN from init0 to nathan" );
      check_nathan_last_history( "get back some coin" );
      check_account_last_history( "init0", "nathan force-transfer 1 BOBCOIN from init0 to nathan" );
      check_account_last_history( "init0", "get back some coin" );

      check_bobcoin_balance( "init0", 20000 );
      check_bobcoin_balance( "nathan", 10000 );

      {
         // Reserve / burn asset
         BOOST_TEST_MESSAGE("Reserve/burn asset");
         con.wallet_api_ptr->reserve_asset("nathan", "1", "BOBCOIN", true);
      }
      BOOST_CHECK(generate_block(app1));

      check_nathan_last_history( "Reserve (burn) 1 BOBCOIN" );

      check_nathan_bobcoin_balance( 0 );

   } catch( fc::exception& e ) {
      edump((e.to_detail_string()));
      throw;
   }
}

BOOST_FIXTURE_TEST_CASE( mpa_tests, cli_fixture )
{
   try
   {
      BOOST_TEST_MESSAGE("Cli MPA Tests");

      INVOKE(upgrade_nathan_account);

      account_object nathan_acct = con.wallet_api_ptr->get_account("nathan");

      auto formatters = con.wallet_api_ptr->get_result_formatters();

      // Create new asset called BOBCOIN backed by CORE
      try
      {
         BOOST_TEST_MESSAGE("Create MPA 'BOBCOIN'");
         graphene::chain::asset_options asset_ops;
         asset_ops.issuer_permissions = ASSET_ISSUER_PERMISSION_ENABLE_BITS_MASK;
         asset_ops.flags = charge_market_fee;
         asset_ops.max_supply = 1000000;
         asset_ops.core_exchange_rate = price(asset(2),asset(1,asset_id_type(1)));
         graphene::chain::bitasset_options bit_opts;
         auto result = con.wallet_api_ptr->create_asset("nathan", "BOBCOIN", 4, asset_ops, bit_opts, true);
         if( formatters.find("create_asset") != formatters.end() )
         {
            BOOST_TEST_MESSAGE("Testing formatter of create_asset");
            string output = formatters["create_asset"](
                  fc::variant(result, FC_PACK_MAX_DEPTH), fc::variants());
            BOOST_CHECK( output.find("BOBCOIN") != string::npos );
         }
      }
      catch(exception& e)
      {
         BOOST_FAIL(e.what());
      }
      catch(...)
      {
         BOOST_FAIL("Unknown exception creating BOBCOIN");
      }
      BOOST_CHECK(generate_block(app1));

      auto check_nathan_last_history = [&]( string keyword ) {
         auto history = con.wallet_api_ptr->get_relative_account_history("nathan", 0, 1, 0);
         BOOST_REQUIRE_GT( history.size(), 0 );
         BOOST_CHECK( history[0].description.find( keyword ) != string::npos );
      };

      check_nathan_last_history( "Create BitAsset" );
      check_nathan_last_history( "BOBCOIN" );

      auto bobcoin = con.wallet_api_ptr->get_asset("BOBCOIN");
      {
         // Update asset
         BOOST_TEST_MESSAGE("Update asset");
         auto options = bobcoin.options;
         BOOST_CHECK_EQUAL( options.max_supply.value, 1000000 );
         options.max_supply = 2000000;
         con.wallet_api_ptr->update_asset("BOBCOIN", {}, options, true);
         // Check
         bobcoin = con.wallet_api_ptr->get_asset("BOBCOIN");
         BOOST_CHECK_EQUAL( bobcoin.options.max_supply.value, 2000000 );
      }
      BOOST_CHECK(generate_block(app1));
      check_nathan_last_history( "Update asset" );

      auto bitbobcoin = con.wallet_api_ptr->get_bitasset_data("BOBCOIN");
      {
         // Update bitasset
         BOOST_TEST_MESSAGE("Update bitasset");
         auto bitoptions = bitbobcoin.options;
         BOOST_CHECK_EQUAL( bitoptions.feed_lifetime_sec, uint32_t(GRAPHENE_DEFAULT_PRICE_FEED_LIFETIME) );
         bitoptions.feed_lifetime_sec = 3600u;
         con.wallet_api_ptr->update_bitasset("BOBCOIN", bitoptions, true);
         // Check
         bitbobcoin = con.wallet_api_ptr->get_bitasset_data("BOBCOIN");
         BOOST_CHECK_EQUAL( bitbobcoin.options.feed_lifetime_sec, 3600u );
      }
      BOOST_CHECK(generate_block(app1));
      check_nathan_last_history( "Update bitasset" );

      {
         // Play with asset fee pool
         auto objs = con.wallet_api_ptr->get_object( object_id_type( bobcoin.dynamic_asset_data_id ) )
                        .as<vector<asset_dynamic_data_object>>( FC_PACK_MAX_DEPTH );
         idump( (objs) );
         BOOST_REQUIRE_EQUAL( objs.size(), 1u );
         asset_dynamic_data_object bobcoin_dyn = objs[0];
         idump( (bobcoin_dyn) );
         share_type old_pool = bobcoin_dyn.fee_pool;

         BOOST_TEST_MESSAGE("Fund fee pool");
         con.wallet_api_ptr->fund_asset_fee_pool("nathan", "BOBCOIN", "2", true);
         objs = con.wallet_api_ptr->get_object( object_id_type( bobcoin.dynamic_asset_data_id ) )
                   .as<vector<asset_dynamic_data_object>>( FC_PACK_MAX_DEPTH );
         BOOST_REQUIRE_EQUAL( objs.size(), 1u );
         bobcoin_dyn = objs[0];
         share_type funded_pool = bobcoin_dyn.fee_pool;
         BOOST_CHECK_EQUAL( funded_pool.value, old_pool.value + GRAPHENE_BLOCKCHAIN_PRECISION * 2 );

         BOOST_CHECK(generate_block(app1));
         check_nathan_last_history( "Fund" );

         BOOST_TEST_MESSAGE("Claim fee pool");
         con.wallet_api_ptr->claim_asset_fee_pool("BOBCOIN", "1", true);
         objs = con.wallet_api_ptr->get_object( object_id_type( bobcoin.dynamic_asset_data_id ) )
                   .as<vector<asset_dynamic_data_object>>( FC_PACK_MAX_DEPTH );
         BOOST_REQUIRE_EQUAL( objs.size(), 1u );
         bobcoin_dyn = objs[0];
         share_type claimed_pool = bobcoin_dyn.fee_pool;
         BOOST_CHECK_EQUAL( claimed_pool.value, old_pool.value + GRAPHENE_BLOCKCHAIN_PRECISION );

         BOOST_CHECK(generate_block(app1));
         check_nathan_last_history( "Claim" );
      }

      {
         // Set price feed producer
         BOOST_TEST_MESSAGE("Set price feed producer");
         asset_bitasset_data_object bob_bitasset = con.wallet_api_ptr->get_bitasset_data( "BOBCOIN" );
         BOOST_CHECK_EQUAL( bob_bitasset.feeds.size(), 0u );

         auto handle = con.wallet_api_ptr->begin_builder_transaction();
         asset_update_feed_producers_operation aufp_op;
         aufp_op.issuer = nathan_acct.id;
         aufp_op.asset_to_update = bobcoin.id;
         aufp_op.new_feed_producers = { nathan_acct.get_id() };
         con.wallet_api_ptr->add_operation_to_builder_transaction( handle, aufp_op );
         con.wallet_api_ptr->set_fees_on_builder_transaction( handle, "1.3.0" );
         con.wallet_api_ptr->sign_builder_transaction( handle, true );

         bob_bitasset = con.wallet_api_ptr->get_bitasset_data( "BOBCOIN" );
         BOOST_CHECK_EQUAL( bob_bitasset.feeds.size(), 1u );
         BOOST_CHECK( bob_bitasset.current_feed.settlement_price.is_null() );

         BOOST_CHECK(generate_block(app1));
         check_nathan_last_history( "Update price feed producers" );
      }

      {
         // Publish price feed
         BOOST_TEST_MESSAGE("Publish price feed");
         price_feed feed;
         feed.settlement_price = price( asset(1,bobcoin.get_id()), asset(2) );
         feed.core_exchange_rate = price( asset(1,bobcoin.get_id()), asset(1) );
         con.wallet_api_ptr->publish_asset_feed( "nathan", "BOBCOIN", feed, true );
         asset_bitasset_data_object bob_bitasset = con.wallet_api_ptr->get_bitasset_data( "BOBCOIN" );
         BOOST_CHECK( bob_bitasset.current_feed.settlement_price == feed.settlement_price );

         BOOST_CHECK(generate_block(app1));
         check_nathan_last_history( "Publish price feed" );
      }

      bool balance_formatter_tested = false;
      auto check_bobcoin_balance = [&](string account, int64_t amount) {
         auto balances = con.wallet_api_ptr->list_account_balances( account );
         size_t count = 0;
         for( auto& bal : balances )
         {
            if( bal.asset_id == bobcoin.id )
            {
               ++count;
               BOOST_CHECK_EQUAL( bal.amount.value, amount );
            }
         }
         BOOST_CHECK_EQUAL(count, 1u);

         // Testing result formatter
         if( !balance_formatter_tested && formatters.find("list_account_balances") != formatters.end() )
         {
            BOOST_TEST_MESSAGE("Testing formatter of list_account_balances");
            string output = formatters["list_account_balances"](
                  fc::variant(balances, FC_PACK_MAX_DEPTH ), fc::variants());
            BOOST_CHECK( output.find("BOBCOIN") != string::npos );
            balance_formatter_tested = true;
         }
      };
      auto check_nathan_bobcoin_balance = [&](int64_t amount) {
         check_bobcoin_balance( "nathan", amount );
      };

      {
         // Borrow
         BOOST_TEST_MESSAGE("Borrow BOBCOIN");
         auto calls = con.wallet_api_ptr->get_call_orders( "BOBCOIN", 10 );
         BOOST_CHECK_EQUAL( calls.size(), 0u );
         con.wallet_api_ptr->borrow_asset( "nathan", "1", "BOBCOIN", "10", true );
         calls = con.wallet_api_ptr->get_call_orders( "BOBCOIN", 10 );
         BOOST_REQUIRE_EQUAL( calls.size(), 1u );
         BOOST_CHECK_EQUAL( calls.front().debt.value, 10000 );

         BOOST_CHECK(generate_block(app1));
         check_nathan_bobcoin_balance( 10000 );
         check_nathan_last_history( "Adjust debt position" );
      }

      {
         // Settle
         BOOST_TEST_MESSAGE("Settle BOBCOIN");
         auto settles = con.wallet_api_ptr->get_settle_orders( "BOBCOIN", 10 );
         BOOST_CHECK_EQUAL( settles.size(), 0u );
         con.wallet_api_ptr->settle_asset( "nathan", "0.2", "BOBCOIN", true );
         settles = con.wallet_api_ptr->get_settle_orders( "BOBCOIN", 10 );
         BOOST_REQUIRE_EQUAL( settles.size(), 1u );
         BOOST_CHECK_EQUAL( settles.front().balance.amount.value, 2000 );

         BOOST_CHECK(generate_block(app1));
         check_nathan_bobcoin_balance( 8000 );
         check_nathan_last_history( "Force-settle" );
      }

      {
         // Transfer
         BOOST_TEST_MESSAGE("Transfer some BOBCOIN to init0");
         con.wallet_api_ptr->transfer2( "nathan", "init0", "0.5", "BOBCOIN", "" );
         con.wallet_api_ptr->transfer( "nathan", "init0", "10000", "1.3.0", "" );

         BOOST_CHECK(generate_block(app1));
         check_bobcoin_balance( "init0", 5000 );
         check_nathan_bobcoin_balance( 3000 );
         check_nathan_last_history( "Transfer" );

      }

      {
         // Nathan places an order
         BOOST_TEST_MESSAGE("Nathan place an order to buy BOBCOIN");
         auto orders = con.wallet_api_ptr->get_limit_orders( "BOBCOIN", "1.3.0", 10 );
         BOOST_CHECK_EQUAL( orders.size(), 0u );
         con.wallet_api_ptr->sell_asset( "nathan", "100", "1.3.0", "1", "BOBCOIN", 300, false, true );
         orders = con.wallet_api_ptr->get_limit_orders( "BOBCOIN", "1.3.0", 10 );
         BOOST_REQUIRE_EQUAL( orders.size(), 1u );
         BOOST_CHECK_EQUAL( orders.front().for_sale.value, 100 * GRAPHENE_BLOCKCHAIN_PRECISION );
         limit_order_id_type nathan_order_id = orders.front().get_id();

         BOOST_CHECK(generate_block(app1));
         check_nathan_bobcoin_balance( 3000 );
         check_nathan_last_history( "Create limit order" );

         // init0 place an order to partially fill Nathan's order
         BOOST_TEST_MESSAGE("init0 place an order to sell BOBCOIN");
         con.wallet_api_ptr->sell_asset( "init0", "0.1", "BOBCOIN", "1", "1.3.0", 200, true, true );
         orders = con.wallet_api_ptr->get_limit_orders( "BOBCOIN", "1.3.0", 10 );
         BOOST_REQUIRE_EQUAL( orders.size(), 1u );
         BOOST_CHECK_EQUAL( orders.front().for_sale.value, 90 * GRAPHENE_BLOCKCHAIN_PRECISION );

         BOOST_CHECK(generate_block(app1));
         check_bobcoin_balance( "init0", 4000 );
         check_nathan_bobcoin_balance( 4000 );
         check_nathan_last_history( "as maker" );

         // Nathan cancel order
         BOOST_TEST_MESSAGE("Nathan cancel order");
         con.wallet_api_ptr->cancel_order( nathan_order_id, true );
         orders = con.wallet_api_ptr->get_limit_orders( "BOBCOIN", "1.3.0", 10 );
         BOOST_CHECK_EQUAL( orders.size(), 0u );

         BOOST_CHECK(generate_block(app1));
         check_nathan_bobcoin_balance( 4000 );
         check_nathan_last_history( "Cancel limit order" );
      }

   } catch( fc::exception& e ) {
      edump((e.to_detail_string()));
      throw;
   }
}

///////////////////////
// Start a server and connect using the same calls as the CLI
// Vote for two witnesses, and make sure they both stay there
// after a maintenance block
///////////////////////
BOOST_FIXTURE_TEST_CASE( cli_vote_for_2_witnesses, cli_fixture )
{
   try
   {
      BOOST_TEST_MESSAGE("Cli Vote Test for 2 Witnesses");

      INVOKE(create_new_account);

      // get the details for init1
      witness_object init1_obj = con.wallet_api_ptr->get_witness("init1");
      int init1_start_votes = init1_obj.total_votes;
      // Vote for a witness
      signed_transaction vote_witness1_tx = con.wallet_api_ptr->vote_for_witness("jmjatlanta", "init1", true, true);

      // generate a block to get things started
      BOOST_CHECK(generate_block(app1));
      // wait for a maintenance interval
      BOOST_CHECK(generate_maintenance_block(app1));

      // Verify that the vote is there
      init1_obj = con.wallet_api_ptr->get_witness("init1");
      witness_object init2_obj = con.wallet_api_ptr->get_witness("init2");
      int init1_middle_votes = init1_obj.total_votes;
      if( !is_hf2262_passed(app1) )
         BOOST_CHECK(init1_middle_votes > init1_start_votes);

      // Vote for a 2nd witness
      int init2_start_votes = init2_obj.total_votes;
      signed_transaction vote_witness2_tx = con.wallet_api_ptr->vote_for_witness("jmjatlanta", "init2", true, true);

      // send another block to trigger maintenance interval
      BOOST_CHECK(generate_maintenance_block(app1));

      // Verify that both the first vote and the 2nd are there
      init2_obj = con.wallet_api_ptr->get_witness("init2");
      init1_obj = con.wallet_api_ptr->get_witness("init1");

      int init2_middle_votes = init2_obj.total_votes;
      if( !is_hf2262_passed(app1) )
         BOOST_CHECK(init2_middle_votes > init2_start_votes);
      int init1_last_votes = init1_obj.total_votes;
      if( !is_hf2262_passed(app1) )
         BOOST_CHECK(init1_last_votes > init1_start_votes);

      {
         auto history = con.wallet_api_ptr->get_account_history_by_operations(
                              "jmjatlanta", {6}, 0, 1); // 6 - account_update_operation
         BOOST_REQUIRE_GT( history.details.size(), 0 );
         BOOST_CHECK( history.details[0].description.find( "Update Account 'jmjatlanta'" ) != string::npos );

         // Testing result formatter
         auto formatters = con.wallet_api_ptr->get_result_formatters();
         if( formatters.find("get_account_history_by_operations") != formatters.end() )
         {
            BOOST_TEST_MESSAGE("Testing formatter of get_account_history_by_operations");
            string output = formatters["get_account_history_by_operations"](
                  fc::variant(history, FC_PACK_MAX_DEPTH), fc::variants());
            BOOST_CHECK( output.find("Update Account 'jmjatlanta'") != string::npos );
         }
      }

   } catch( fc::exception& e ) {
      edump((e.to_detail_string()));
      throw;
   }
}

BOOST_FIXTURE_TEST_CASE( cli_get_signed_transaction_signers, cli_fixture )
{
   try
   {
      INVOKE(upgrade_nathan_account);

      // register account and transfer funds
      const auto test_bki = con.wallet_api_ptr->suggest_brain_key();
      con.wallet_api_ptr->register_account(
         "test", test_bki.pub_key, test_bki.pub_key, "nathan", "nathan", 0, true
      );
      con.wallet_api_ptr->transfer("nathan", "test", "1000", "1.3.0", "", true);

      // import key and save wallet
      BOOST_CHECK(con.wallet_api_ptr->import_key("test", test_bki.wif_priv_key));
      con.wallet_api_ptr->save_wallet_file(con.wallet_filename);

      // create transaction and check expected result
      auto signed_trx = con.wallet_api_ptr->transfer("test", "nathan", "10", "1.3.0", "", true);

      const auto &test_acc = con.wallet_api_ptr->get_account("test");
      flat_set<public_key_type> expected_signers = {test_bki.pub_key};
      vector<flat_set<account_id_type> > expected_key_refs{{test_acc.get_id(), test_acc.get_id()}};

      auto signers = con.wallet_api_ptr->get_transaction_signers(signed_trx);
      BOOST_CHECK(signers == expected_signers);

      auto key_refs = con.wallet_api_ptr->get_key_references({expected_signers.begin(), expected_signers.end()});
      BOOST_CHECK(key_refs == expected_key_refs);

   } catch( fc::exception& e ) {
      edump((e.to_detail_string()));
      throw;
   }
}


///////////////////////
// Wallet RPC
// Test adding an unnecessary signature to a transaction
///////////////////////
BOOST_FIXTURE_TEST_CASE(cli_sign_tx_with_unnecessary_signature, cli_fixture) {
   try {
      auto db = app1->chain_database();

      account_object nathan_acct = con.wallet_api_ptr->get_account("nathan");
      INVOKE(upgrade_nathan_account);

      // Register Bob account
      const auto bob_bki = con.wallet_api_ptr->suggest_brain_key();
      con.wallet_api_ptr->register_account(
              "bob", bob_bki.pub_key, bob_bki.pub_key, "nathan", "nathan", 0, true
      );

      // Register Charlie account
      const graphene::wallet::brain_key_info charlie_bki = con.wallet_api_ptr->suggest_brain_key();
      con.wallet_api_ptr->register_account(
              "charlie", charlie_bki.pub_key, charlie_bki.pub_key, "nathan", "nathan", 0, true
      );
      const account_object &charlie_acc = con.wallet_api_ptr->get_account("charlie");

      // Import Bob's key
      BOOST_CHECK(con.wallet_api_ptr->import_key("bob", bob_bki.wif_priv_key));

      // Create transaction with a transfer operation from Nathan to Charlie
      transfer_operation top;
      top.from = nathan_acct.id;
      top.to = charlie_acc.id;
      top.amount = asset(5000);
      top.fee = db->current_fee_schedule().calculate_fee(top);

      signed_transaction test_tx;
      test_tx.operations.push_back(top);

      // Sign the transaction with the implied nathan's key and the explicitly yet unnecessary Bob's key
      auto signed_trx = con.wallet_api_ptr->sign_transaction2(test_tx, {bob_bki.pub_key}, false);

      // Check for two signatures on the transaction
      BOOST_CHECK_EQUAL(signed_trx.signatures.size(), 2);
      flat_set<public_key_type> signers = con.wallet_api_ptr->get_transaction_signers(signed_trx);

      // Check that the signed transaction contains both Nathan's required signature and Bob's unnecessary signature
      BOOST_CHECK_EQUAL(nathan_acct.active.get_keys().size(), 1);
      flat_set<public_key_type> expected_signers = {bob_bki.pub_key, nathan_acct.active.get_keys().front()};
      flat_set<public_key_type> actual_signers = con.wallet_api_ptr->get_transaction_signers(signed_trx);
      BOOST_CHECK(signers == expected_signers);

   } catch (fc::exception &e) {
      edump((e.to_detail_string()));
      throw;
   }
}


///////////////////////
// Wallet RPC
// Test adding an unnecessary signature to a transaction builder
///////////////////////
BOOST_FIXTURE_TEST_CASE(cli_sign_tx_builder_with_unnecessary_signature, cli_fixture) {
   try {
      auto db = app1->chain_database();

      account_object nathan_acct = con.wallet_api_ptr->get_account("nathan");
      INVOKE(upgrade_nathan_account);

      // Register Bob account
      const auto bob_bki = con.wallet_api_ptr->suggest_brain_key();
      con.wallet_api_ptr->register_account(
              "bob", bob_bki.pub_key, bob_bki.pub_key, "nathan", "nathan", 0, true
      );

      // Register Charlie account
      const graphene::wallet::brain_key_info charlie_bki = con.wallet_api_ptr->suggest_brain_key();
      con.wallet_api_ptr->register_account(
              "charlie", charlie_bki.pub_key, charlie_bki.pub_key, "nathan", "nathan", 0, true
      );
      const account_object &charlie_acc = con.wallet_api_ptr->get_account("charlie");

      // Import Bob's key
      BOOST_CHECK(con.wallet_api_ptr->import_key("bob", bob_bki.wif_priv_key));

      // Use transaction builder to build a transaction with a transfer operation from Nathan to Charlie
      graphene::wallet::transaction_handle_type tx_handle = con.wallet_api_ptr->begin_builder_transaction();

      transfer_operation top;
      top.from = nathan_acct.id;
      top.to = charlie_acc.id;
      top.amount = asset(5000);

      con.wallet_api_ptr->add_operation_to_builder_transaction(tx_handle, top);
      con.wallet_api_ptr->set_fees_on_builder_transaction(tx_handle, GRAPHENE_SYMBOL);

      // Sign the transaction with the implied nathan's key and the explicitly yet unnecessary Bob's key
      auto signed_trx = con.wallet_api_ptr->sign_builder_transaction2(tx_handle, {bob_bki.pub_key}, false);

      // Check for two signatures on the transaction
      BOOST_CHECK_EQUAL(signed_trx.signatures.size(), 2);
      flat_set<public_key_type> signers = con.wallet_api_ptr->get_transaction_signers(signed_trx);

      // Check that the signed transaction contains both Nathan's required signature and Bob's unnecessary signature
      BOOST_CHECK_EQUAL(nathan_acct.active.get_keys().size(), 1);
      flat_set<public_key_type> expected_signers = {bob_bki.pub_key, nathan_acct.active.get_keys().front()};
      flat_set<public_key_type> actual_signers = con.wallet_api_ptr->get_transaction_signers(signed_trx);
      BOOST_CHECK(signers == expected_signers);

   } catch (fc::exception &e) {
      edump((e.to_detail_string()));
      throw;
   }
}


BOOST_FIXTURE_TEST_CASE( cli_get_available_transaction_signers, cli_fixture )
{
   try
   {
      INVOKE(upgrade_nathan_account);

      // register account
      const auto test_bki = con.wallet_api_ptr->suggest_brain_key();
      con.wallet_api_ptr->register_account(
         "test", test_bki.pub_key, test_bki.pub_key, "nathan", "nathan", 0, true
      );
      const auto &test_acc = con.wallet_api_ptr->get_account("test");

      // create and sign transaction
      signed_transaction trx;
      trx.operations = {transfer_operation()};

      // sign with test key
      const auto test_privkey = wif_to_key( test_bki.wif_priv_key );
      BOOST_REQUIRE( test_privkey );
      trx.sign( *test_privkey, con.wallet_data.chain_id );

      // sign with other keys
      const auto privkey_1 = fc::ecc::private_key::generate();
      trx.sign( privkey_1, con.wallet_data.chain_id );

      const auto privkey_2 = fc::ecc::private_key::generate();
      trx.sign( privkey_2, con.wallet_data.chain_id );

      // verify expected result
      flat_set<public_key_type> expected_signers = {test_bki.pub_key,
                                                    privkey_1.get_public_key(),
                                                    privkey_2.get_public_key()};

      auto signers = con.wallet_api_ptr->get_transaction_signers(trx);
      BOOST_CHECK(signers == expected_signers);

      // blockchain has no references to unknown accounts (privkey_1, privkey_2)
      // only test account available
      vector<flat_set<account_id_type> > expected_key_refs;
      expected_key_refs.push_back(flat_set<account_id_type>());
      expected_key_refs.push_back(flat_set<account_id_type>());
      expected_key_refs.push_back({test_acc.get_id()});

      auto key_refs = con.wallet_api_ptr->get_key_references({expected_signers.begin(), expected_signers.end()});
      std::sort(key_refs.begin(), key_refs.end());

      BOOST_CHECK(key_refs == expected_key_refs);

   } catch( fc::exception& e ) {
      edump((e.to_detail_string()));
      throw;
   }
}

BOOST_FIXTURE_TEST_CASE( cli_cant_get_signers_from_modified_transaction, cli_fixture )
{
   try
   {
      INVOKE(upgrade_nathan_account);

      // register account
      const auto test_bki = con.wallet_api_ptr->suggest_brain_key();
      con.wallet_api_ptr->register_account(
         "test", test_bki.pub_key, test_bki.pub_key, "nathan", "nathan", 0, true
      );

      // create and sign transaction
      signed_transaction trx;
      trx.operations = {transfer_operation()};

      // sign with test key
      const auto test_privkey = wif_to_key( test_bki.wif_priv_key );
      BOOST_REQUIRE( test_privkey );
      trx.sign( *test_privkey, con.wallet_data.chain_id );

      // modify transaction (MITM-attack)
      trx.operations.clear();

      // verify if transaction has no valid signature of test account
      flat_set<public_key_type> expected_signers_of_valid_transaction = {test_bki.pub_key};
      auto signers = con.wallet_api_ptr->get_transaction_signers(trx);
      BOOST_CHECK(signers != expected_signers_of_valid_transaction);

   } catch( fc::exception& e ) {
      edump((e.to_detail_string()));
      throw;
   }
}

///////////////////
// Start a server and connect using the same calls as the CLI
// Set a voting proxy and be assured that it sticks
///////////////////
BOOST_FIXTURE_TEST_CASE( cli_set_voting_proxy, cli_fixture )
{
   try {
      INVOKE(create_new_account);

      // grab account for comparison
      account_object prior_voting_account = con.wallet_api_ptr->get_account("jmjatlanta");
      // set the voting proxy to nathan
      BOOST_TEST_MESSAGE("About to set voting proxy.");
      signed_transaction voting_tx = con.wallet_api_ptr->set_voting_proxy("jmjatlanta", "nathan", true);
      account_object after_voting_account = con.wallet_api_ptr->get_account("jmjatlanta");
      // see if it changed
      BOOST_CHECK(prior_voting_account.options.voting_account != after_voting_account.options.voting_account);
   } catch( fc::exception& e ) {
      edump((e.to_detail_string()));
      throw;
   }
}

///////////////////
// Test blind transactions and mantissa length of range proofs.
///////////////////
BOOST_FIXTURE_TEST_CASE( cli_confidential_tx_test, cli_fixture )
{
   using namespace graphene::wallet;
   try {
      // we need to increase the default max transaction size to run this test.
      this->app1->chain_database()->modify(
         this->app1->chain_database()->get_global_properties(),
         []( global_property_object& p) {
            p.parameters.maximum_transaction_size = 8192;
      });
      std::vector<signed_transaction> import_txs;

      BOOST_TEST_MESSAGE("Importing nathan's balance");
      import_txs = con.wallet_api_ptr->import_balance("nathan", nathan_keys, true);

      unsigned int head_block = 0;
      auto & W = *con.wallet_api_ptr; // Wallet alias

      auto formatters = con.wallet_api_ptr->get_result_formatters();

      BOOST_TEST_MESSAGE("Creating blind accounts");
      graphene::wallet::brain_key_info bki_nathan = W.suggest_brain_key();
      graphene::wallet::brain_key_info bki_alice = W.suggest_brain_key();
      graphene::wallet::brain_key_info bki_bob = W.suggest_brain_key();
      W.create_blind_account("nathan", bki_nathan.brain_priv_key);
      W.create_blind_account("alice", bki_alice.brain_priv_key);
      W.create_blind_account("bob", bki_bob.brain_priv_key);
      BOOST_CHECK(W.get_blind_accounts().size() == 3);

      // ** Block 1: Import Nathan account:
      BOOST_TEST_MESSAGE("Importing nathan key and balance");
      std::vector<std::string> nathan_keys{"5KQwrPbwdL6PhXujxW37FSSQZ1JiwsST4cqQzDeyXtP79zkvFD3"};
      W.import_key("nathan", nathan_keys[0]);
      W.import_balance("nathan", nathan_keys, true);
      generate_block(app1); head_block++;

      // ** Block 2: Nathan will blind 100M CORE token:
      BOOST_TEST_MESSAGE("Blinding a large balance");
      {
         auto result = W.transfer_to_blind("nathan", GRAPHENE_SYMBOL, {{"nathan","100000000"}}, true);
         // Testing result formatter
         if( formatters.find("transfer_to_blind") != formatters.end() )
         {
            BOOST_TEST_MESSAGE("Testing formatter of transfer_to_blind");
            string output = formatters["transfer_to_blind"](
                  fc::variant(result, FC_PACK_MAX_DEPTH), fc::variants());
            BOOST_CHECK( output.find("receipt") != string::npos );
         }
      }
      BOOST_CHECK( W.get_blind_balances("nathan")[0].amount == 10000000000000 );
      generate_block(app1); head_block++;

      // ** Block 3: Nathan will send 1M CORE token to alice and 10K CORE token to bob. We
      // then confirm that balances are received, and then analyze the range
      // prooofs to make sure the mantissa length does not reveal approximate
      // balance (issue #480).
      std::map<std::string, share_type> to_list = {{"alice",100000000000LL},
                                                   {"bob",    1000000000LL}};
      vector<blind_confirmation> bconfs;
      auto core_asset = W.get_asset("1.3.0");
      BOOST_TEST_MESSAGE("Sending blind transactions to alice and bob");
      for (auto to : to_list) {
         string amount = core_asset.amount_to_string(to.second);
         bconfs.push_back(W.blind_transfer("nathan",to.first,amount,core_asset.symbol,true));
         BOOST_CHECK( W.get_blind_balances(to.first)[0].amount == to.second );
      }
      BOOST_TEST_MESSAGE("Inspecting range proof mantissa lengths");
      vector<int> rp_mantissabits;
      for (auto conf : bconfs) {
         for (auto out : conf.trx.operations[0].get<blind_transfer_operation>().outputs) {
            rp_mantissabits.push_back(1+out.range_proof[1]); // 2nd byte encodes mantissa length
         }
      }
      // We are checking the mantissa length of the range proofs for several Pedersen
      // commitments of varying magnitude.  We don't want the mantissa lengths to give
      // away magnitude.  Deprecated wallet behavior was to use "just enough" mantissa
      // bits to prove range, but this gives away value to within a factor of two. As a
      // naive test, we assume that if all mantissa lengths are equal, then they are not
      // revealing magnitude.  However, future more-sophisticated wallet behavior
      // *might* randomize mantissa length to achieve some space savings in the range
      // proof.  The following test will fail in that case and a more sophisticated test
      // will be needed.
      auto adjacent_unequal = std::adjacent_find(rp_mantissabits.begin(),
                                                 rp_mantissabits.end(),         // find unequal adjacent values
                                                 std::not_equal_to<int>());
      BOOST_CHECK(adjacent_unequal == rp_mantissabits.end());
      generate_block(app1); head_block++;

      // ** Check head block:
      BOOST_TEST_MESSAGE("Check that all expected blocks have processed");
      dynamic_global_property_object dgp = W.get_dynamic_global_properties();
      BOOST_CHECK(dgp.head_block_number == head_block);

      // Receive blind transfer
      {
         auto result = W.receive_blind_transfer(bconfs[1].outputs[1].confirmation_receipt, "", "bob_receive");
         BOOST_CHECK_EQUAL( result.amount.amount.value, 1000000000 );
         // Testing result formatter
         if( formatters.find("receive_blind_transfer") != formatters.end() )
         {
            BOOST_TEST_MESSAGE("Testing formatter of receive_blind_transfer");
            string output = formatters["receive_blind_transfer"](
                  fc::variant(result, FC_PACK_MAX_DEPTH), fc::variants());
            BOOST_CHECK( output.find("bob_receive") != string::npos );
         }
      }

      // Check blind history
      {
         auto result = W.blind_history("nathan");
         BOOST_CHECK_EQUAL( result.size(), 5u ); // 1 transfer_to_blind + 2 outputs * 2 blind_transfers
         // Testing result formatter
         if( formatters.find("blind_history") != formatters.end() )
         {
            BOOST_TEST_MESSAGE("Testing formatter of blind_history");
            string output = formatters["blind_history"](
                  fc::variant(result, FC_PACK_MAX_DEPTH), fc::variants());
            BOOST_CHECK( output.find("WHEN") != string::npos );
            BOOST_TEST_MESSAGE( output );
         }
      }
   } catch( fc::exception& e ) {
      edump((e.to_detail_string()));
      throw;
   }
}

/******
 * Check account history pagination (see bitshares-core/issue/1176)
 */
BOOST_FIXTURE_TEST_CASE( account_history_pagination, cli_fixture )
{
   try
   {
      INVOKE(create_new_account);

      // attempt to give jmjatlanta some esher
      BOOST_TEST_MESSAGE("Transferring esher from Nathan to jmjatlanta");
      for(int i = 1; i <= 199; i++)
      {
         signed_transaction transfer_tx = con.wallet_api_ptr->transfer("nathan", "jmjatlanta", std::to_string(i),
                                                "1.3.0", "Here are some CORE token for your new account", true);
      }

      BOOST_CHECK(generate_block(app1));

      // now get account history and make sure everything is there (and no duplicates)
      std::vector<graphene::wallet::operation_detail> history = con.wallet_api_ptr->get_account_history("jmjatlanta", 300);
      BOOST_CHECK_EQUAL(201u, history.size() );

      std::set<object_id_type> operation_ids;

      for(auto& op : history)
      {
         if( operation_ids.find(op.op.id) != operation_ids.end() )
         {
            BOOST_FAIL("Duplicate found");
         }
         operation_ids.insert(op.op.id);
      }

      // Testing result formatter
      auto formatters = con.wallet_api_ptr->get_result_formatters();
      if( formatters.find("get_account_history") != formatters.end() )
      {
         BOOST_TEST_MESSAGE("Testing formatter of get_account_history");
         string output = formatters["get_account_history"](
               fc::variant(history, FC_PACK_MAX_DEPTH), fc::variants());
         BOOST_CHECK( output.find("Here are some") != string::npos );
      }
   } catch( fc::exception& e ) {
      edump((e.to_detail_string()));
      throw;
   }
}


///////////////////////
// Create a multi-sig account and verify that only when all signatures are
// signed, the transaction could be broadcast
///////////////////////
BOOST_AUTO_TEST_CASE( cli_multisig_transaction )
{
   using namespace graphene::chain;
   using namespace graphene::app;
   std::shared_ptr<graphene::app::application> app1;
   try {
      fc::temp_directory app_dir( graphene::utilities::temp_directory_path() );

      int server_port_number = 0;
      app1 = start_application(app_dir, server_port_number);

      // connect to the server
      client_connection con(app1, app_dir, server_port_number);

      BOOST_TEST_MESSAGE("Setting wallet password");
      con.wallet_api_ptr->set_password("supersecret");
      con.wallet_api_ptr->unlock("supersecret");

      // import Nathan account
      BOOST_TEST_MESSAGE("Importing nathan key");
      std::vector<std::string> nathan_keys{"5KQwrPbwdL6PhXujxW37FSSQZ1JiwsST4cqQzDeyXtP79zkvFD3"};
      BOOST_CHECK_EQUAL(nathan_keys[0], "5KQwrPbwdL6PhXujxW37FSSQZ1JiwsST4cqQzDeyXtP79zkvFD3");
      BOOST_CHECK(con.wallet_api_ptr->import_key("nathan", nathan_keys[0]));

      BOOST_TEST_MESSAGE("Importing nathan's balance");
      std::vector<signed_transaction> import_txs = con.wallet_api_ptr->import_balance("nathan", nathan_keys, true);
      account_object nathan_acct_before_upgrade = con.wallet_api_ptr->get_account("nathan");

      // upgrade nathan
      BOOST_TEST_MESSAGE("Upgrading Nathan to LTM");
      signed_transaction upgrade_tx = con.wallet_api_ptr->upgrade_account("nathan", true);
      account_object nathan_acct_after_upgrade = con.wallet_api_ptr->get_account("nathan");

      // verify that the upgrade was successful
      BOOST_CHECK_PREDICATE( std::not_equal_to<uint32_t>(), (nathan_acct_before_upgrade.membership_expiration_date.sec_since_epoch())(nathan_acct_after_upgrade.membership_expiration_date.sec_since_epoch()) );
      BOOST_CHECK(nathan_acct_after_upgrade.is_lifetime_member());

      // create a new multisig account
      graphene::wallet::brain_key_info bki1 = con.wallet_api_ptr->suggest_brain_key();
      graphene::wallet::brain_key_info bki2 = con.wallet_api_ptr->suggest_brain_key();
      graphene::wallet::brain_key_info bki3 = con.wallet_api_ptr->suggest_brain_key();
      graphene::wallet::brain_key_info bki4 = con.wallet_api_ptr->suggest_brain_key();
      BOOST_CHECK(!bki1.brain_priv_key.empty());
      BOOST_CHECK(!bki2.brain_priv_key.empty());
      BOOST_CHECK(!bki3.brain_priv_key.empty());
      BOOST_CHECK(!bki4.brain_priv_key.empty());

      signed_transaction create_multisig_acct_tx;
      account_create_operation account_create_op;

      account_create_op.referrer = nathan_acct_after_upgrade.id;
      account_create_op.referrer_percent = nathan_acct_after_upgrade.referrer_rewards_percentage;
      account_create_op.registrar = nathan_acct_after_upgrade.id;
      account_create_op.name = "cifer.test";
      account_create_op.owner = authority(1, bki1.pub_key, 1);
      account_create_op.active = authority(2, bki2.pub_key, 1, bki3.pub_key, 1);
      account_create_op.options.memo_key = bki4.pub_key;
      account_create_op.fee = asset(1000000);  // should be enough for creating account

      create_multisig_acct_tx.operations.push_back(account_create_op);
      con.wallet_api_ptr->sign_transaction(create_multisig_acct_tx, true);

      // attempt to give cifer.test some esher
      BOOST_TEST_MESSAGE("Transferring esher from Nathan to cifer.test");
      signed_transaction transfer_tx1 = con.wallet_api_ptr->transfer("nathan", "cifer.test", "10000", "1.3.0", "Here are some ESH for your new account", true);

      // transfer bts from cifer.test to nathan
      BOOST_TEST_MESSAGE("Transferring esher from cifer.test to nathan");
      auto dyn_props = app1->chain_database()->get_dynamic_global_properties();
      account_object cifer_test = con.wallet_api_ptr->get_account("cifer.test");

      // construct a transfer transaction
      signed_transaction transfer_tx2;
      transfer_operation xfer_op;
      xfer_op.from = cifer_test.id;
      xfer_op.to = nathan_acct_after_upgrade.id;
      xfer_op.amount = asset(100000000);
      xfer_op.fee = asset(3000000);  // should be enough for transfer
      transfer_tx2.operations.push_back(xfer_op);

      // case1: sign a transaction without TaPoS and expiration fields
      // expect: return a transaction with TaPoS and expiration filled
      transfer_tx2 =
         con.wallet_api_ptr->add_transaction_signature( transfer_tx2, false );
      BOOST_CHECK( ( transfer_tx2.ref_block_num != 0 &&
                     transfer_tx2.ref_block_prefix != 0 ) ||
                   ( transfer_tx2.expiration != fc::time_point_sec() ) );

      // case2: broadcast without signature
      // expect: exception with missing active authority
      BOOST_CHECK_THROW(con.wallet_api_ptr->broadcast_transaction(transfer_tx2), fc::exception);

      // case3:
      // import one of the private keys for this new account in the wallet file,
      // sign and broadcast with partial signatures
      //
      // expect: exception with missing active authority
      BOOST_CHECK(con.wallet_api_ptr->import_key("cifer.test", bki2.wif_priv_key));
      BOOST_CHECK_THROW(con.wallet_api_ptr->add_transaction_signature(transfer_tx2, true), fc::exception);

      // case4: sign again as signature exists
      // expect: num of signatures not increase
      transfer_tx2 = con.wallet_api_ptr->add_transaction_signature(transfer_tx2, false);
      BOOST_CHECK_EQUAL(transfer_tx2.signatures.size(), 1);

      // case5:
      // import another private key, sign and broadcast without full signatures
      //
      // expect: transaction broadcast successfully
      BOOST_CHECK(con.wallet_api_ptr->import_key("cifer.test", bki3.wif_priv_key));
      con.wallet_api_ptr->add_transaction_signature(transfer_tx2, true);
      auto balances = con.wallet_api_ptr->list_account_balances( "cifer.test" );
      for (auto b : balances) {
         if (b.asset_id == asset_id_type()) {
            BOOST_CHECK(b == asset(900000000 - 3000000));
         }
      }

   } catch( fc::exception& e ) {
      edump((e.to_detail_string()));
      throw;
   }
}

graphene::wallet::plain_keys decrypt_keys( const std::string& password, const vector<char>& cipher_keys )
{
   auto pw = fc::sha512::hash( password.c_str(), password.size() );
   vector<char> decrypted = fc::aes_decrypt( pw, cipher_keys );
   return fc::raw::unpack<graphene::wallet::plain_keys>( decrypted );
}

BOOST_AUTO_TEST_CASE( saving_keys_wallet_test ) {
   cli_fixture cli;

   cli.con.wallet_api_ptr->import_balance( "nathan", cli.nathan_keys, true );
   cli.con.wallet_api_ptr->upgrade_account( "nathan", true );
   std::string brain_key( "FICTIVE WEARY MINIBUS LENS HAWKIE MAIDISH MINTY GLYPH GYTE KNOT COCKSHY LENTIGO PROPS BIFORM KHUTBAH BRAZIL" );
   cli.con.wallet_api_ptr->create_account_with_brain_key( brain_key, "account1", "nathan", "nathan", true );

   BOOST_CHECK_NO_THROW( cli.con.wallet_api_ptr->transfer( "nathan", "account1", "9000", "1.3.0", "", true ) );

   std::string path( cli.app_dir.path().generic_string() + "/wallet.json" );
   graphene::wallet::wallet_data wallet = fc::json::from_file( path ).as<graphene::wallet::wallet_data>( 2 * GRAPHENE_MAX_NESTED_OBJECTS );
   BOOST_CHECK( wallet.extra_keys.size() == 1 ); // nathan
   BOOST_CHECK( wallet.pending_account_registrations.size() == 1 ); // account1
   BOOST_CHECK( wallet.pending_account_registrations["account1"].size() == 2 ); // account1 active key + account1 memo key

   graphene::wallet::plain_keys pk = decrypt_keys( "supersecret", wallet.cipher_keys );
   BOOST_CHECK( pk.keys.size() == 1 ); // nathan key

   BOOST_CHECK( generate_block( cli.app1 ) );
   // Intentional delay
   fc::usleep( fc::seconds(1) );

   wallet = fc::json::from_file( path ).as<graphene::wallet::wallet_data>( 2 * GRAPHENE_MAX_NESTED_OBJECTS );
   BOOST_CHECK( wallet.extra_keys.size() == 2 ); // nathan + account1
   BOOST_CHECK( wallet.pending_account_registrations.empty() );
   BOOST_CHECK_NO_THROW( cli.con.wallet_api_ptr->transfer( "account1", "nathan", "1000", "1.3.0", "", true ) );

   pk = decrypt_keys( "supersecret", wallet.cipher_keys );
   BOOST_CHECK( pk.keys.size() == 3 ); // nathan key + account1 active key + account1 memo key
}


///////////////////////
// Start a server and connect using the same calls as the CLI
// Create an HTLC
///////////////////////
BOOST_AUTO_TEST_CASE( cli_create_htlc )
{
   using namespace graphene::chain;
   using namespace graphene::app;
   std::shared_ptr<graphene::app::application> app1;
   try {
      fc::temp_directory app_dir( graphene::utilities::temp_directory_path() );

      int server_port_number = 0;
      app1 = start_application(app_dir, server_port_number);
      // set committee parameters
      app1->chain_database()->modify(app1->chain_database()->get_global_properties(), [](global_property_object& p) {
         graphene::chain::htlc_options params;
         params.max_preimage_size = 1024;
         params.max_timeout_secs = 60 * 60 * 24 * 28;
         p.parameters.extensions.value.updatable_htlc_options = params;
      });

      // connect to the server
      client_connection con(app1, app_dir, server_port_number);

      BOOST_TEST_MESSAGE("Setting wallet password");
      con.wallet_api_ptr->set_password("supersecret");
      con.wallet_api_ptr->unlock("supersecret");

      // import Nathan account
      BOOST_TEST_MESSAGE("Importing nathan key");
      std::vector<std::string> nathan_keys{"5KQwrPbwdL6PhXujxW37FSSQZ1JiwsST4cqQzDeyXtP79zkvFD3"};
      BOOST_CHECK_EQUAL(nathan_keys[0], "5KQwrPbwdL6PhXujxW37FSSQZ1JiwsST4cqQzDeyXtP79zkvFD3");
      BOOST_CHECK(con.wallet_api_ptr->import_key("nathan", nathan_keys[0]));

      BOOST_TEST_MESSAGE("Importing nathan's balance");
      std::vector<signed_transaction> import_txs = con.wallet_api_ptr->import_balance("nathan", nathan_keys, true);
      account_object nathan_acct_before_upgrade = con.wallet_api_ptr->get_account("nathan");

      // upgrade nathan
      BOOST_TEST_MESSAGE("Upgrading Nathan to LTM");
      signed_transaction upgrade_tx = con.wallet_api_ptr->upgrade_account("nathan", true);
      account_object nathan_acct_after_upgrade = con.wallet_api_ptr->get_account("nathan");

      // verify that the upgrade was successful
      BOOST_CHECK_PREDICATE( std::not_equal_to<uint32_t>(), (nathan_acct_before_upgrade.membership_expiration_date.sec_since_epoch())
            (nathan_acct_after_upgrade.membership_expiration_date.sec_since_epoch()) );
      BOOST_CHECK(nathan_acct_after_upgrade.is_lifetime_member());

      // Create new asset called BOBCOIN
      try
      {
         graphene::chain::asset_options asset_ops;
         asset_ops.max_supply = 1000000;
         asset_ops.core_exchange_rate = price(asset(2),asset(1,asset_id_type(1)));
         fc::optional<graphene::chain::bitasset_options> bit_opts;
         con.wallet_api_ptr->create_asset("nathan", "BOBCOIN", 5, asset_ops, bit_opts, true);
      }
      catch(exception& e)
      {
         BOOST_FAIL(e.what());
      }
      catch(...)
      {
         BOOST_FAIL("Unknown exception creating BOBCOIN");
      }

      // create a new account for Alice
      {
         graphene::wallet::brain_key_info bki = con.wallet_api_ptr->suggest_brain_key();
         BOOST_CHECK(!bki.brain_priv_key.empty());
         signed_transaction create_acct_tx = con.wallet_api_ptr->create_account_with_brain_key(bki.brain_priv_key,
               "alice", "nathan", "nathan", true);
         con.wallet_api_ptr->save_wallet_file(con.wallet_filename);
         // attempt to give alice some esher
         BOOST_TEST_MESSAGE("Transferring esher from Nathan to alice");
         signed_transaction transfer_tx = con.wallet_api_ptr->transfer("nathan", "alice", "10000", "1.3.0",
               "Here are some CORE token for your new account", true);
      }

      // create a new account for Bob
      {
         graphene::wallet::brain_key_info bki = con.wallet_api_ptr->suggest_brain_key();
         BOOST_CHECK(!bki.brain_priv_key.empty());
         signed_transaction create_acct_tx = con.wallet_api_ptr->create_account_with_brain_key(bki.brain_priv_key,
               "bob", "nathan", "nathan", true);
         // this should cause resync which will import the keys of alice and bob
         generate_block(app1);
         // attempt to give bob some esher
         BOOST_TEST_MESSAGE("Transferring esher from Nathan to Bob");
         signed_transaction transfer_tx = con.wallet_api_ptr->transfer("nathan", "bob", "10000", "1.3.0",
               "Here are some CORE token for your new account", true);
         con.wallet_api_ptr->issue_asset("bob", "5", "BOBCOIN", "Here are your BOBCOINs", true);
      }

      BOOST_TEST_MESSAGE("Alice has agreed to buy 3 BOBCOIN from Bob for 3 ESH. Alice creates an HTLC");
      // create an HTLC
      std::string preimage_string = "My Secret";
      fc::sha256 preimage_md = fc::sha256::hash(preimage_string);
      std::stringstream ss;
      for(size_t i = 0; i < preimage_md.data_size(); i++)
      {
         char d = preimage_md.data()[i];
         unsigned char uc = static_cast<unsigned char>(d);
         ss << std::setfill('0') << std::setw(2) << std::hex << (int)uc;
      }
      std::string hash_str = ss.str();
      BOOST_TEST_MESSAGE("Secret is " + preimage_string + " and hash is " + hash_str);
      uint32_t timelock = fc::days(1).to_seconds();
      graphene::chain::signed_transaction result_tx
            = con.wallet_api_ptr->htlc_create("alice", "bob",
            "3", "1.3.0", "SHA256", hash_str, preimage_string.size(), timelock, "", true);

      // normally, a wallet would watch block production, and find the transaction. Here, we can cheat:
      std::string alice_htlc_id_as_string;
      htlc_id_type alice_htlc_id;
      {
         BOOST_TEST_MESSAGE("The system is generating a block");
         graphene::chain::signed_block result_block;
         BOOST_CHECK(generate_block(app1, result_block));

         // get the ID:
         auto tmp_hist = con.wallet_api_ptr->get_account_history("alice", 1);
         htlc_id_type htlc_id { tmp_hist[0].op.result.get<object_id_type>() };
         alice_htlc_id = htlc_id;
         alice_htlc_id_as_string = (std::string)(object_id_type)htlc_id;
         BOOST_TEST_MESSAGE("Alice shares the HTLC ID with Bob. The HTLC ID is: " + alice_htlc_id_as_string);
      }

      // Bob can now look over Alice's HTLC, to see if it is what was agreed to.
      BOOST_TEST_MESSAGE("Bob retrieves the HTLC Object by ID to examine it.");
      auto alice_htlc = con.wallet_api_ptr->get_htlc(alice_htlc_id);
      BOOST_TEST_MESSAGE("The HTLC Object is: " + fc::json::to_pretty_string(alice_htlc));

      // Bob likes what he sees, so he creates an HTLC, using the info he retrieved from Alice's HTLC
      con.wallet_api_ptr->htlc_create("bob", "alice",
            "3", "BOBCOIN", "SHA256", hash_str, preimage_string.size(), timelock, "", true);

      // normally, a wallet would watch block production, and find the transaction. Here, we can cheat:
      std::string bob_htlc_id_as_string;
      htlc_id_type bob_htlc_id;
      {
         BOOST_TEST_MESSAGE("The system is generating a block");
         graphene::chain::signed_block result_block;
         BOOST_CHECK(generate_block(app1, result_block));

         // get the ID:
         auto tmp_hist = con.wallet_api_ptr->get_account_history("bob", 1);
         htlc_id_type htlc_id { tmp_hist[0].op.result.get<object_id_type>() };
         bob_htlc_id = htlc_id;
         bob_htlc_id_as_string = (std::string)(object_id_type)htlc_id;
         BOOST_TEST_MESSAGE("Bob shares the HTLC ID with Alice. The HTLC ID is: " + bob_htlc_id_as_string);
      }

      // Alice can now look over Bob's HTLC, to see if it is what was agreed to:
      BOOST_TEST_MESSAGE("Alice retrieves the HTLC Object by ID to examine it.");
      auto bob_htlc = con.wallet_api_ptr->get_htlc(bob_htlc_id);
      BOOST_TEST_MESSAGE("The HTLC Object is: " + fc::json::to_pretty_string(bob_htlc));

      // Alice likes what she sees, so uses her preimage to get her BOBCOIN
      {
         BOOST_TEST_MESSAGE("Alice uses her preimage to retrieve the BOBCOIN");
         std::string secret = "My Secret";
         con.wallet_api_ptr->htlc_redeem(bob_htlc_id, "alice", secret, true);
         BOOST_TEST_MESSAGE("The system is generating a block");
         BOOST_CHECK(generate_block(app1));
      }

      // TODO: Bob can look at Alice's history to see her preimage
      // Bob can use the preimage to retrieve his ESH
      {
         BOOST_TEST_MESSAGE("Bob uses Alice's preimage to retrieve the BOBCOIN");
         std::string secret = "My Secret";
         con.wallet_api_ptr->htlc_redeem(alice_htlc_id, "bob", secret, true);
         BOOST_TEST_MESSAGE("The system is generating a block");
         BOOST_CHECK(generate_block(app1));
      }

      // test operation_printer
      auto hist = con.wallet_api_ptr->get_account_history("alice", 10);
      for(size_t i = 0; i < hist.size(); ++i)
      {
         auto obj = hist[i];
         std::stringstream ss;
         ss << "Description: " << obj.description << "\n";
         auto str = ss.str();
         BOOST_TEST_MESSAGE( str );
         if (i == 3 || i == 4)
         {
            BOOST_CHECK( str.find("SHA256 8a45f62f47") != std::string::npos );
         }
      }
   } catch( fc::exception& e ) {
      edump((e.to_detail_string()));
      throw;
   }
}

static string encapsulate( const graphene::wallet::signed_message& msg )
{
   fc::stringstream encapsulated;
   encapsulated << "-----BEGIN ESHER SIGNED MESSAGE-----\n"
                << msg.message << '\n'
                << "-----BEGIN META-----\n"
                << "account=" << msg.meta.account << '\n'
                << "memokey=" << std::string( msg.meta.memo_key ) << '\n'
                << "block=" << msg.meta.block << '\n'
                << "timestamp=" << msg.meta.time << '\n'
                << "-----BEGIN SIGNATURE-----\n"
                << fc::to_hex( (const char*)msg.signature->data(), msg.signature->size() ) << '\n'
                << "-----END ESHER SIGNED MESSAGE-----";
   return encapsulated.str();
}

/******
 * Check signing/verifying a message with a memo key
 */
BOOST_FIXTURE_TEST_CASE( cli_sign_message, cli_fixture )
{ try {
   const auto nathan_priv = *wif_to_key( nathan_keys[0] );
   const public_key_type nathan_pub( nathan_priv.get_public_key() );

   // account does not exist
   BOOST_REQUIRE_THROW( con.wallet_api_ptr->sign_message( "dan", "123" ), fc::assert_exception );

   // success
   graphene::wallet::signed_message msg = con.wallet_api_ptr->sign_message( "nathan", "123" );
   BOOST_CHECK_EQUAL( "123", msg.message );
   BOOST_CHECK_EQUAL( "nathan", msg.meta.account );
   BOOST_CHECK_EQUAL( std::string( nathan_pub ), std::string( msg.meta.memo_key ) );
   BOOST_CHECK( msg.signature.valid() );

   // change message, verify failure
   msg.message = "124";
   BOOST_CHECK( !con.wallet_api_ptr->verify_message( msg.message, msg.meta.account, msg.meta.block, msg.meta.time,
                                                     *msg.signature ) );
   BOOST_CHECK( !con.wallet_api_ptr->verify_signed_message( msg ) );
   BOOST_CHECK( !con.wallet_api_ptr->verify_encapsulated_message( encapsulate( msg ) ) );
   msg.message = "123";

   // change account, verify failure
   // nonexistent account:
   msg.meta.account = "dan";
   BOOST_REQUIRE_THROW( con.wallet_api_ptr->verify_message( msg.message, msg.meta.account, msg.meta.block,
                                                            msg.meta.time, *msg.signature ), fc::assert_exception );
   BOOST_REQUIRE_THROW( con.wallet_api_ptr->verify_signed_message( msg ), fc::assert_exception );
   BOOST_REQUIRE_THROW( con.wallet_api_ptr->verify_encapsulated_message( encapsulate( msg ) ), fc::assert_exception);
   // existing, but wrong account:
   msg.meta.account = "committee-account";
   BOOST_CHECK( !con.wallet_api_ptr->verify_message( msg.message, msg.meta.account, msg.meta.block,
                                                     msg.meta.time, *msg.signature ) );
   BOOST_CHECK( !con.wallet_api_ptr->verify_signed_message( msg ) );
   BOOST_CHECK( !con.wallet_api_ptr->verify_encapsulated_message( encapsulate( msg ) ) );
   msg.meta.account = "nathan";

   // change key, verify failure
   ++msg.meta.memo_key.key_data.data()[1];
   //BOOST_CHECK( !con.wallet_api_ptr->verify_message( msg.message, msg.meta.account, msg.meta.block, msg.meta.time,
   //                                                  *msg.signature ) );
   BOOST_CHECK( !con.wallet_api_ptr->verify_signed_message( msg ) );
   BOOST_CHECK( !con.wallet_api_ptr->verify_encapsulated_message( encapsulate( msg ) ) );
   --msg.meta.memo_key.key_data.data()[1];

   // change block, verify failure
   ++msg.meta.block;
   BOOST_CHECK( !con.wallet_api_ptr->verify_message( msg.message, msg.meta.account, msg.meta.block, msg.meta.time,
                                                     *msg.signature ) );
   BOOST_CHECK( !con.wallet_api_ptr->verify_signed_message( msg ) );
   BOOST_CHECK( !con.wallet_api_ptr->verify_encapsulated_message( encapsulate( msg ) ) );
   --msg.meta.block;

   // change time, verify failure
   ++msg.meta.time[0];
   BOOST_CHECK( !con.wallet_api_ptr->verify_message( msg.message, msg.meta.account, msg.meta.block, msg.meta.time,
                                                     *msg.signature ) );
   BOOST_CHECK( !con.wallet_api_ptr->verify_signed_message( msg ) );
   BOOST_CHECK( !con.wallet_api_ptr->verify_encapsulated_message( encapsulate( msg ) ) );
   --msg.meta.time[0];

   // change signature, verify failure
   ++msg.signature->data()[1];
   try {
      BOOST_CHECK( !con.wallet_api_ptr->verify_message( msg.message, msg.meta.account, msg.meta.block, msg.meta.time,
                                                        *msg.signature ) );
   } catch( const fc::assert_exception& ) {} // failure to reconstruct key from signature is ok as well
   try {
      BOOST_CHECK( !con.wallet_api_ptr->verify_signed_message( msg ) );
   } catch( const fc::assert_exception& ) {} // failure to reconstruct key from signature is ok as well
   try {
      BOOST_CHECK( !con.wallet_api_ptr->verify_encapsulated_message( encapsulate( msg ) ) );
   } catch( const fc::assert_exception& ) {} // failure to reconstruct key from signature is ok as well
   --msg.signature->data()[1];

   // verify success
   BOOST_CHECK( con.wallet_api_ptr->verify_message( msg.message, msg.meta.account, msg.meta.block, msg.meta.time,
                                                    *msg.signature ) );
   BOOST_CHECK( con.wallet_api_ptr->verify_signed_message( msg ) );
   BOOST_CHECK( con.wallet_api_ptr->verify_encapsulated_message( encapsulate( msg ) ) );

} FC_LOG_AND_RETHROW() }

///////////////////
// Test the general storage by custom operations plugin
///////////////////
BOOST_FIXTURE_TEST_CASE( general_storage, cli_fixture )
{
   try {
      // create the taker account
      INVOKE(create_new_account);

      auto db = app1->chain_database();

      BOOST_TEST_MESSAGE("Storing in a map.");

      flat_map<string, optional<string>> pairs;
      pairs["key1"] = fc::json::to_string("value1");
      pairs["key2"] = fc::json::to_string("value2");

      con.wallet_api_ptr->account_store_map("nathan", "any", false, pairs, true);

      BOOST_TEST_MESSAGE("The system is generating a block.");
      BOOST_CHECK(generate_block(app1));

      BOOST_TEST_MESSAGE("Get current map for nathan.");
      auto nathan_map = con.wallet_api_ptr->get_account_storage("nathan", "any");

      BOOST_CHECK_EQUAL(nathan_map[0].id.instance(), 0);
      BOOST_CHECK_EQUAL(nathan_map[0].account.instance.value, 17);
      BOOST_CHECK_EQUAL(nathan_map[0].catalog, "any");
      BOOST_CHECK_EQUAL(nathan_map[0].key, "key1");
      BOOST_CHECK_EQUAL(nathan_map[0].value->as_string(), "value1");
      BOOST_CHECK_EQUAL(nathan_map[1].id.instance(), 1);
      BOOST_CHECK_EQUAL(nathan_map[1].account.instance.value, 17);
      BOOST_CHECK_EQUAL(nathan_map[1].catalog, "any");
      BOOST_CHECK_EQUAL(nathan_map[1].key, "key2");
      BOOST_CHECK_EQUAL(nathan_map[1].value->as_string(), "value2");

      BOOST_TEST_MESSAGE("Storing in a list.");

      flat_map<string, optional<string>> favs;
      favs["chocolate"];
      favs["milk"];
      favs["banana"];

      con.wallet_api_ptr->account_store_map("nathan", "favourites", false, favs, true);

      BOOST_TEST_MESSAGE("The system is generating a block.");
      BOOST_CHECK(generate_block(app1));

      BOOST_TEST_MESSAGE("Get current list for nathan.");
      auto nathan_list = con.wallet_api_ptr->get_account_storage("nathan", "favourites");

      BOOST_CHECK_EQUAL(nathan_list[0].id.instance(), 2);
      BOOST_CHECK_EQUAL(nathan_list[0].account.instance.value, 17);
      BOOST_CHECK_EQUAL(nathan_list[0].catalog, "favourites");
      BOOST_CHECK_EQUAL(nathan_list[0].key, "banana");
      BOOST_CHECK_EQUAL(nathan_list[1].id.instance(), 3);
      BOOST_CHECK_EQUAL(nathan_list[1].account.instance.value, 17);
      BOOST_CHECK_EQUAL(nathan_list[1].catalog, "favourites");
      BOOST_CHECK_EQUAL(nathan_list[1].key, "chocolate");
      BOOST_CHECK_EQUAL(nathan_list[2].id.instance(), 4);
      BOOST_CHECK_EQUAL(nathan_list[2].account.instance.value, 17);
      BOOST_CHECK_EQUAL(nathan_list[2].catalog, "favourites");
      BOOST_CHECK_EQUAL(nathan_list[2].key, "milk");

   } catch( fc::exception& e ) {
      edump((e.to_detail_string()));
      throw;
   }
}

//////
// Template copied
//////
template<typename Object>
unsigned_int member_index(string name) {
   unsigned_int index;
   fc::typelist::runtime::for_each(typename fc::reflector<Object>::native_members(), [&name, &index](auto t) mutable {
      if (name == decltype(t)::type::get_name())
      index = decltype(t)::type::index;
   });
   return index;
}

///////////////////////
// Wallet RPC
// Test sign_builder_transaction2 with an account (bob) that has received a custom authorization
// to transfer funds from another account (alice)
///////////////////////
BOOST_FIXTURE_TEST_CASE(cli_use_authorized_transfer, cli_fixture) {
   try {
      //////
      // Initialize the blockchain
      //////
      auto db = app1->chain_database();

      account_object nathan_acct = con.wallet_api_ptr->get_account("nathan");
      INVOKE(upgrade_nathan_account);

      // Register Alice account
      const auto alice_bki = con.wallet_api_ptr->suggest_brain_key();
      con.wallet_api_ptr->register_account(
              "alice", alice_bki.pub_key, alice_bki.pub_key, "nathan", "nathan", 0, true
      );
      const account_object &alice_acc = con.wallet_api_ptr->get_account("alice");

      // Register Bob account
      const auto bob_bki = con.wallet_api_ptr->suggest_brain_key();
      con.wallet_api_ptr->register_account(
              "bob", bob_bki.pub_key, bob_bki.pub_key, "nathan", "nathan", 0, true
      );
      const account_object &bob_acc = con.wallet_api_ptr->get_account("bob");

      // Register Charlie account
      const graphene::wallet::brain_key_info charlie_bki = con.wallet_api_ptr->suggest_brain_key();
      con.wallet_api_ptr->register_account(
              "charlie", charlie_bki.pub_key, charlie_bki.pub_key, "nathan", "nathan", 0, true
      );
      const account_object &charlie_acc = con.wallet_api_ptr->get_account("charlie");

      // Fund Alice's account
      con.wallet_api_ptr->transfer("nathan", "alice", "450000", "1.3.0", "", true);

      // Initialize common variables
      signed_transaction signed_trx;


      //////
      // Initialize Alice's CLI wallet
      //////
      client_connection con_alice(app1, app_dir, server_port_number, "wallet_alice.json");
      con_alice.wallet_api_ptr->set_password("supersecret");
      con_alice.wallet_api_ptr->unlock("supersecret");

      // Import Alice's key
      BOOST_CHECK(con_alice.wallet_api_ptr->import_key("alice", alice_bki.wif_priv_key));


      //////
      // Initialize the blockchain for BSIP 40
      //////
      generate_blocks(app1, HARDFORK_BSIP_40_TIME);
      // Set committee parameters
      app1->chain_database()->modify(app1->chain_database()->get_global_properties(), [](global_property_object& p) {
         p.parameters.extensions.value.custom_authority_options = custom_authority_options_type();
      });


      //////
      // Alice authorizes Bob to transfer funds from her account to Charlie's account
      //////
      graphene::wallet::transaction_handle_type tx_alice_handle = con_alice.wallet_api_ptr->begin_builder_transaction();

      custom_authority_create_operation caop;
      caop.account = alice_acc.get_id();
      caop.auth.add_authority(bob_acc.get_id(), 1);
      caop.auth.weight_threshold = 1;
      caop.enabled = true;
      caop.valid_to = db->head_block_time() + 1000;
      caop.operation_type = operation::tag<transfer_operation>::value;

      // Restriction should have "to" equal Charlie
      vector<restriction> restrictions;
      auto to_index = member_index<transfer_operation>("to");
      restrictions.emplace_back(to_index, restriction::func_eq, charlie_acc.get_id());

      con_alice.wallet_api_ptr->add_operation_to_builder_transaction(tx_alice_handle, caop);
      asset ca_fee = con_alice.wallet_api_ptr->set_fees_on_builder_transaction(tx_alice_handle, GRAPHENE_SYMBOL);

      // Sign the transaction with the inferred Alice key
      signed_trx = con_alice.wallet_api_ptr->sign_builder_transaction2(tx_alice_handle, {}, true);

      // Check for one signatures on the transaction
      BOOST_CHECK_EQUAL(signed_trx.signatures.size(), 1);

      // Check that the signed transaction contains Alice's signature
      flat_set<public_key_type> expected_signers = {alice_bki.pub_key};
      flat_set<public_key_type> actual_signers = con_alice.wallet_api_ptr->get_transaction_signers(signed_trx);
      BOOST_CHECK(actual_signers == expected_signers);


      //////
      // Initialize Bob's CLI wallet
      //////
      client_connection con_bob(app1, app_dir, server_port_number, "wallet_bob.json");
      con_bob.wallet_api_ptr->set_password("supersecret");
      con_bob.wallet_api_ptr->unlock("supersecret");

      // Import Bob's key
      BOOST_CHECK(con_bob.wallet_api_ptr->import_key("bob", bob_bki.wif_priv_key));


      //////
      // Bob attempt to transfer funds from Alice to Charlie while using Bob's wallet
      // This should succeed because Bob is authorized to transfer by Alice
      //////
      graphene::wallet::transaction_handle_type tx_bob_handle = con_bob.wallet_api_ptr->begin_builder_transaction();

      const asset transfer_amount = asset(123 * GRAPHENE_BLOCKCHAIN_PRECISION);
      transfer_operation top;
      top.from = alice_acc.id;
      top.to = charlie_acc.id;
      top.amount = transfer_amount;

      con_bob.wallet_api_ptr->add_operation_to_builder_transaction(tx_bob_handle, top);
      asset transfer_fee = con_bob.wallet_api_ptr->set_fees_on_builder_transaction(tx_bob_handle, GRAPHENE_SYMBOL);

      // Sign the transaction with the explicit Bob key
      signed_trx = con_bob.wallet_api_ptr->sign_builder_transaction2(tx_bob_handle, {bob_bki.pub_key}, true);

      // Check for one signatures on the transaction
      BOOST_CHECK_EQUAL(signed_trx.signatures.size(), 1);

      // Check that the signed transaction contains Bob's signature
      BOOST_CHECK_EQUAL(nathan_acct.active.get_keys().size(), 1);
      expected_signers = {bob_bki.pub_key};
      actual_signers = con_bob.wallet_api_ptr->get_transaction_signers(signed_trx);
      BOOST_CHECK(actual_signers == expected_signers);


      //////
      // Check account balances
      //////
      // Check Charlie's balances
      vector<asset> charlie_balances = con.wallet_api_ptr->list_account_balances("charlie");
      BOOST_CHECK_EQUAL(charlie_balances.size(), 1);
      asset charlie_core_balance = charlie_balances.front();
      asset expected_charlie_core_balance = transfer_amount;
      BOOST_CHECK(charlie_core_balance == expected_charlie_core_balance);

      // Check Bob's balances
      vector<asset> bob_balances = con.wallet_api_ptr->list_account_balances("bob");
      BOOST_CHECK_EQUAL(bob_balances.size(), 0);

      // Check Alice's balance
      vector<asset> alice_balances = con.wallet_api_ptr->list_account_balances("alice");
      BOOST_CHECK_EQUAL(alice_balances.size(), 1);
      asset alice_core_balance = alice_balances.front();
      asset expected_alice_balance =  asset(450000 * GRAPHENE_BLOCKCHAIN_PRECISION)
                                      - expected_charlie_core_balance
                                      - ca_fee - transfer_fee;
      BOOST_CHECK(alice_core_balance.asset_id == expected_alice_balance.asset_id);
      BOOST_CHECK_EQUAL(alice_core_balance.amount.value, expected_alice_balance.amount.value);

   } catch (fc::exception &e) {
      edump((e.to_detail_string()));
      throw;
   }
}

BOOST_AUTO_TEST_CASE( cli_create_htlc_bsip64 )
{
   using namespace graphene::chain;
   using namespace graphene::app;
   std::shared_ptr<graphene::app::application> app1;
   try {
      fc::temp_directory app_dir( graphene::utilities::temp_directory_path() );

      int server_port_number = 0;
      app1 = start_application(app_dir, server_port_number);
      // set committee parameters
      app1->chain_database()->modify(app1->chain_database()->get_global_properties(), [](global_property_object& p)
      {
         graphene::chain::htlc_options params;
         params.max_preimage_size = 1024;
         params.max_timeout_secs = 60 * 60 * 24 * 28;
         p.parameters.extensions.value.updatable_htlc_options = params;
      });

      // connect to the server
      client_connection con(app1, app_dir, server_port_number);

      // get past hardforks
      generate_blocks( app1, HARDFORK_CORE_BSIP64_TIME + 10);

      BOOST_TEST_MESSAGE("Setting wallet password");
      con.wallet_api_ptr->set_password("supersecret");
      con.wallet_api_ptr->unlock("supersecret");

      // import Nathan account
      BOOST_TEST_MESSAGE("Importing nathan key");
      std::vector<std::string> nathan_keys{"5KQwrPbwdL6PhXujxW37FSSQZ1JiwsST4cqQzDeyXtP79zkvFD3"};
      BOOST_CHECK_EQUAL(nathan_keys[0], "5KQwrPbwdL6PhXujxW37FSSQZ1JiwsST4cqQzDeyXtP79zkvFD3");
      BOOST_CHECK(con.wallet_api_ptr->import_key("nathan", nathan_keys[0]));

      BOOST_TEST_MESSAGE("Importing nathan's balance");
      std::vector<signed_transaction> import_txs = con.wallet_api_ptr->import_balance("nathan", nathan_keys, true);
      account_object nathan_acct_before_upgrade = con.wallet_api_ptr->get_account("nathan");

      // upgrade nathan
      BOOST_TEST_MESSAGE("Upgrading Nathan to LTM");
      signed_transaction upgrade_tx = con.wallet_api_ptr->upgrade_account("nathan", true);
      account_object nathan_acct_after_upgrade = con.wallet_api_ptr->get_account("nathan");

      // verify that the upgrade was successful
      BOOST_CHECK_PREDICATE( std::not_equal_to<uint32_t>(),
            (nathan_acct_before_upgrade.membership_expiration_date.sec_since_epoch())
            (nathan_acct_after_upgrade.membership_expiration_date.sec_since_epoch()) );
      BOOST_CHECK(nathan_acct_after_upgrade.is_lifetime_member());

      // Create new asset called BOBCOIN
      try
      {
         graphene::chain::asset_options asset_ops;
         asset_ops.max_supply = 1000000;
         asset_ops.core_exchange_rate = price(asset(2),asset(1,asset_id_type(1)));
         fc::optional<graphene::chain::bitasset_options> bit_opts;
         con.wallet_api_ptr->create_asset("nathan", "BOBCOIN", 5, asset_ops, bit_opts, true);
      }
      catch(exception& e)
      {
         BOOST_FAIL(e.what());
      }
      catch(...)
      {
         BOOST_FAIL("Unknown exception creating BOBCOIN");
      }

      // create a new account for Alice
      {
         graphene::wallet::brain_key_info bki = con.wallet_api_ptr->suggest_brain_key();
         BOOST_CHECK(!bki.brain_priv_key.empty());
         signed_transaction create_acct_tx = con.wallet_api_ptr->create_account_with_brain_key(bki.brain_priv_key,
               "alice", "nathan", "nathan", true);
         con.wallet_api_ptr->save_wallet_file(con.wallet_filename);
         // attempt to give alice some esher
         BOOST_TEST_MESSAGE("Transferring esher from Nathan to alice");
         signed_transaction transfer_tx = con.wallet_api_ptr->transfer("nathan", "alice", "10000", "1.3.0",
               "Here are some CORE token for your new account", true);
      }

      // create a new account for Bob
      {
         graphene::wallet::brain_key_info bki = con.wallet_api_ptr->suggest_brain_key();
         BOOST_CHECK(!bki.brain_priv_key.empty());
         signed_transaction create_acct_tx = con.wallet_api_ptr->create_account_with_brain_key(bki.brain_priv_key,
               "bob", "nathan", "nathan", true);
         // this should cause resync which will import the keys of alice and bob
         generate_block(app1);
         // attempt to give bob some esher
         BOOST_TEST_MESSAGE("Transferring esher from Nathan to Bob");
         signed_transaction transfer_tx = con.wallet_api_ptr->transfer("nathan", "bob", "10000", "1.3.0",
               "Here are some CORE token for your new account", true);
         con.wallet_api_ptr->issue_asset("bob", "5", "BOBCOIN", "Here are your BOBCOINs", true);
      }

      BOOST_TEST_MESSAGE("Alice has agreed to buy 3 BOBCOIN from Bob for 3 ESH. Alice creates an HTLC");
      // create an HTLC
      std::string preimage_string = "My Super Long Secret that is larger than 50 charaters. How do I look?\n";
      fc::hash160 preimage_md = fc::hash160::hash(preimage_string);
      std::stringstream ss;
      for(size_t i = 0; i < preimage_md.data_size(); i++)
      {
         char d = preimage_md.data()[i];
         unsigned char uc = static_cast<unsigned char>(d);
         ss << std::setfill('0') << std::setw(2) << std::hex << (int)uc;
      }
      std::string hash_str = ss.str();
      BOOST_TEST_MESSAGE("Secret is " + preimage_string + " and hash is " + hash_str);
      uint32_t timelock = fc::days(1).to_seconds();
      graphene::chain::signed_transaction result_tx
            = con.wallet_api_ptr->htlc_create("alice", "bob",
            "3", "1.3.0", "HASH160", hash_str, preimage_string.size(), timelock, "Alice to Bob", true);

      // normally, a wallet would watch block production, and find the transaction. Here, we can cheat:
      std::string alice_htlc_id_as_string;
      htlc_id_type alice_htlc_id;
      {
         BOOST_TEST_MESSAGE("The system is generating a block");
         graphene::chain::signed_block result_block;
         BOOST_CHECK(generate_block(app1, result_block));

         // get the ID:
         auto tmp_hist = con.wallet_api_ptr->get_account_history("alice", 1);
         htlc_id_type htlc_id { tmp_hist[0].op.result.get<object_id_type>() };
         alice_htlc_id = htlc_id;
         alice_htlc_id_as_string = (std::string)(object_id_type)htlc_id;
         BOOST_TEST_MESSAGE("Alice shares the HTLC ID with Bob. The HTLC ID is: " + alice_htlc_id_as_string);
      }

      // Bob can now look over Alice's HTLC, to see if it is what was agreed to.
      BOOST_TEST_MESSAGE("Bob retrieves the HTLC Object by ID to examine it.");
      auto alice_htlc = con.wallet_api_ptr->get_htlc(alice_htlc_id);
      BOOST_TEST_MESSAGE("The HTLC Object is: " + fc::json::to_pretty_string(alice_htlc));

      // Bob likes what he sees, so he creates an HTLC, using the info he retrieved from Alice's HTLC
      con.wallet_api_ptr->htlc_create("bob", "alice",
            "3", "BOBCOIN", "HASH160", hash_str, preimage_string.size(), fc::hours(12).to_seconds(),
            "Bob to Alice", true);

      // normally, a wallet would watch block production, and find the transaction. Here, we can cheat:
      std::string bob_htlc_id_as_string;
      htlc_id_type bob_htlc_id;
      {
         BOOST_TEST_MESSAGE("The system is generating a block");
         graphene::chain::signed_block result_block;
         BOOST_CHECK(generate_block(app1, result_block));

         // get the ID:
         auto tmp_hist = con.wallet_api_ptr->get_account_history("bob", 1);
         htlc_id_type htlc_id { tmp_hist[0].op.result.get<object_id_type>() };
         bob_htlc_id = htlc_id;
         bob_htlc_id_as_string = (std::string)(object_id_type)htlc_id;
         BOOST_TEST_MESSAGE("Bob shares the HTLC ID with Alice. The HTLC ID is: " + bob_htlc_id_as_string);
      }

      // Alice can now look over Bob's HTLC, to see if it is what was agreed to:
      BOOST_TEST_MESSAGE("Alice retrieves the HTLC Object by ID to examine it.");
      auto bob_htlc = con.wallet_api_ptr->get_htlc(bob_htlc_id);
      BOOST_TEST_MESSAGE("The HTLC Object is: " + fc::json::to_pretty_string(bob_htlc));

      // Alice likes what she sees, so uses her preimage to get her BOBCOIN
      {
         BOOST_TEST_MESSAGE("Alice uses her preimage to retrieve the BOBCOIN");
         con.wallet_api_ptr->htlc_redeem(bob_htlc_id, "alice", preimage_string, true);
         BOOST_TEST_MESSAGE("The system is generating a block");
         BOOST_CHECK(generate_block(app1));
      }

      // Bob can look at Alice's history to see her preimage
      {
         BOOST_TEST_MESSAGE("Bob can look at the history of Alice to see the preimage");
         std::vector<graphene::wallet::operation_detail> hist = con.wallet_api_ptr->get_account_history("alice", 1);
         BOOST_CHECK( hist[0].description.find("with preimage \"4d792") != hist[0].description.npos);
      }

      // Bob can also look at his own history to see Alice's preimage
      {
         BOOST_TEST_MESSAGE("Bob can look at his own history to see the preimage");
         std::vector<graphene::wallet::operation_detail> hist = con.wallet_api_ptr->get_account_history("bob", 1);
         BOOST_CHECK( hist[0].description.find("with preimage \"4d792") != hist[0].description.npos);
      }

      // Bob can use the preimage to retrieve his ESH
      {
         BOOST_TEST_MESSAGE("Bob uses Alice's preimage to retrieve the BOBCOIN");
         con.wallet_api_ptr->htlc_redeem(alice_htlc_id, "bob", preimage_string, true);
         BOOST_TEST_MESSAGE("The system is generating a block");
         BOOST_CHECK(generate_block(app1));
      }

      // test operation_printer
      auto hist = con.wallet_api_ptr->get_account_history("alice", 10);
      for(size_t i = 0; i < hist.size(); ++i)
      {
         auto obj = hist[i];
         std::stringstream ss;
         ss << "Description: " << obj.description << "\n";
         auto str = ss.str();
         BOOST_TEST_MESSAGE( str );
         if (i == 3 || i == 4)
         {
            BOOST_CHECK( str.find("HASH160 620e4d5ba") != std::string::npos );
         }
      }
      con.wallet_api_ptr->lock();
      hist = con.wallet_api_ptr->get_account_history("alice", 10);
      for(size_t i = 0; i < hist.size(); ++i)
      {
         auto obj = hist[i];
         std::stringstream ss;
         ss << "Description: " << obj.description << "\n";
         auto str = ss.str();
         BOOST_TEST_MESSAGE( str );
         if (i == 3 || i == 4)
         {
            BOOST_CHECK( str.find("HASH160 620e4d5ba") != std::string::npos );
         }
      }
      con.wallet_api_ptr->unlock("supersecret");

   } catch( fc::exception& e ) {
      edump((e.to_detail_string()));
      throw;
   }
}
