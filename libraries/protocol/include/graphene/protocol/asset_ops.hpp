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
#pragma once
#include <graphene/protocol/base.hpp>
#include <graphene/protocol/asset.hpp>
#include <graphene/protocol/memo.hpp>

namespace graphene { namespace protocol {

   struct additional_asset_options
   {
      fc::optional<uint16_t>                  reward_percent;
      fc::optional<flat_set<account_id_type>> whitelist_market_fee_sharing;
      // After BSIP81 activation, taker_fee_percent is the taker fee
      fc::optional<uint16_t>                  taker_fee_percent;
   };
   typedef extension<additional_asset_options> additional_asset_options_t;

   bool is_valid_symbol( const string& symbol );

   /**
    * @brief The asset_options struct contains options available on all assets in the network
    *
    * @note Changes to this struct will break protocol compatibility
    */
   struct asset_options {
      /// The maximum supply of this asset which may exist at any given time. This can be as large as
      /// GRAPHENE_MAX_SHARE_SUPPLY
      share_type max_supply = GRAPHENE_MAX_SHARE_SUPPLY;
      /// When this asset is traded on the markets, this percentage of the total traded will be exacted and paid
      /// to the issuer. This is a fixed point value, representing hundredths of a percent, i.e. a value of 100
      /// in this field means a 1% fee is charged on market trades of this asset.
      // BSIP81: Asset owners may specify different market fee rate for maker orders and taker orders
      // After BSIP81 activation, market_fee_percent is the maker fee
      uint16_t market_fee_percent = 0;
      /// Market fees calculated as @ref market_fee_percent of the traded volume are capped to this value
      share_type max_market_fee = GRAPHENE_MAX_SHARE_SUPPLY;

      /// The flags which the issuer has permission to update. See @ref asset_issuer_permission_flags
      uint16_t issuer_permissions = DEFAULT_UIA_ASSET_ISSUER_PERMISSION;
      /// The currently active flags on this permission. See @ref asset_issuer_permission_flags
      uint16_t flags = 0;

      /// @return the bits in @ref flags which are allowed to be updated according to data in @ref issuer_permissions
      uint16_t get_enabled_issuer_permissions_mask() const;

      /// When a non-core asset is used to pay a fee, the blockchain must convert that asset to core asset in
      /// order to accept the fee. If this asset's fee pool is funded, the chain will automatically deposite fees
      /// in this asset to its accumulated fees, and withdraw from the fee pool the same amount as converted at
      /// the core exchange rate.
      price core_exchange_rate = price(asset(), asset(0, asset_id_type(1)));

      /// A set of accounts which maintain whitelists to consult for this asset. If whitelist_authorities
      /// is non-empty, then only accounts in whitelist_authorities are allowed to hold, use, or transfer the asset.
      flat_set<account_id_type> whitelist_authorities;
      /// A set of accounts which maintain blacklists to consult for this asset. If flags & white_list is set,
      /// an account may only send, receive, trade, etc. in this asset if none of these accounts appears in
      /// its account_object::blacklisting_accounts field. If the account is blacklisted, it may not transact in
      /// this asset even if it is also whitelisted.
      flat_set<account_id_type> blacklist_authorities;

      /** defines the assets that this asset may be traded against in the market */
      flat_set<asset_id_type>   whitelist_markets;
      /** defines the assets that this asset may not be traded against in the market, must not overlap whitelist */
      flat_set<asset_id_type>   blacklist_markets;

      /**
       * data that describes the meaning/purpose of this asset, fee will be charged proportional to
       * size of description.
       */
      string description;
      additional_asset_options_t extensions;

      /// Perform internal consistency checks.
      /// @throws fc::exception if any check fails
      void validate()const;

      /// Perform checks about @ref flags.
      /// @throws fc::exception if any check fails
      void validate_flags( bool is_market_issued, bool allow_disable_collateral_bid = true )const;
   };

   /**
    * @brief The bitasset_options struct contains configurable options available only to BitAssets.
    *
    * @note Changes to this struct will break protocol compatibility
    */
   struct bitasset_options {

      /// Defines how a BitAsset would respond to black swan events
      enum class black_swan_response_type
      {
         /// All debt positions are closed, all or some collateral is moved to a global-settlement fund.
         /// Debt asset holders can claim collateral via force-settlement.
         /// It is not allowed to create new debt positions when the fund is not empty.
         global_settlement = 0,
         /// No debt position is closed, and the derived settlement price is dynamically capped at the collateral
         /// ratio of the debt position with the least collateral ratio so that all debt positions are able to pay
         /// off their debt when being margin called or force-settled.
         /// It is allowed to create new debt positions and update existing debt positions.
         /// Also known as "Global Settlement Protection".
         no_settlement = 1,
         /// Only the undercollateralized debt positions are closed and their collateral is moved to a fund which
         /// can be claimed via force-settlement. The derived settlement price is capped at the fund's collateral
         /// ratio so that remaining debt positions will not be margin called or force-settled at a worse price
         /// when the fund is not empty.
         /// It is allowed to create new debt positions and update existing debt positions.
         individual_settlement_to_fund = 2,
         /// Only the undercollateralized debt positions are closed and their collateral is moved to a limit order
         /// on the order book which can be bought. The derived settlement price is NOT capped, which means remaining
         /// debt positions could be margin called at a worse price.
         /// It is allowed to create new debt positions and update existing debt positions.
         individual_settlement_to_order = 3,
         /// Total number of available black swan response methods
         BSRM_TYPE_COUNT = 4
      };

      struct ext
      {
         /// After BSIP77, when creating a new debt position or updating an existing position,
         /// the position will be checked against this parameter.
         /// Unused for prediction markets, although we allow it to be set for simpler implementation
         fc::optional<uint16_t> initial_collateral_ratio;  // BSIP-77
         /// After BSIP75, the asset owner can update MCR directly
         fc::optional<uint16_t> maintenance_collateral_ratio; // BSIP-75
         /// After BSIP75, the asset owner can update MSSR directly
         fc::optional<uint16_t> maximum_short_squeeze_ratio;  // BSIP-75
         fc::optional<uint16_t> margin_call_fee_ratio; // BSIP 74
         fc::optional<uint16_t> force_settle_fee_percent;  // BSIP-87
         // https://github.com/bitshares/bitshares-core/issues/2467
         fc::optional<uint8_t> black_swan_response_method;
      };

      /// Time before a price feed expires
      uint32_t feed_lifetime_sec = GRAPHENE_DEFAULT_PRICE_FEED_LIFETIME;
      /// Minimum number of unexpired feeds required to extract a median feed from
      uint8_t minimum_feeds = 1;
      /// This is the delay between the time a long requests settlement and the chain evaluates the settlement
      uint32_t force_settlement_delay_sec = GRAPHENE_DEFAULT_FORCE_SETTLEMENT_DELAY;
      /// This is the percent to adjust the feed price in the short's favor in the event of a forced settlement
      uint16_t force_settlement_offset_percent = GRAPHENE_DEFAULT_FORCE_SETTLEMENT_OFFSET;
      /// Force settlement volume can be limited such that only a certain percentage of the total existing supply
      /// of the asset may be force-settled within any given chain maintenance interval. This field stores the
      /// percentage of the current supply which may be force settled within the current maintenance interval. If
      /// force settlements come due in an interval in which the maximum volume has already been settled, the new
      /// settlements will be enqueued and processed at the beginning of the next maintenance interval.
      uint16_t maximum_force_settlement_volume = GRAPHENE_DEFAULT_FORCE_SETTLEMENT_MAX_VOLUME;
      /// This speicifies which asset type is used to collateralize short sales
      /// This field may only be updated if the current supply of the asset is zero.
      asset_id_type short_backing_asset;

      extension<ext> extensions;

      /// Perform internal consistency checks.
      /// @throws fc::exception if any check fails
      void validate()const;

      /// Get the effective black swan response method
      black_swan_response_type get_black_swan_response_method() const
      {
         if( !extensions.value.black_swan_response_method.valid() )
            return black_swan_response_type::global_settlement;
         return static_cast<black_swan_response_type>( *extensions.value.black_swan_response_method );
      }
   };


   /**
    * @ingroup operations
    */
   struct asset_create_operation : public base_operation
   {
      struct fee_params_t {
         uint64_t symbol3        = 500000 * GRAPHENE_BLOCKCHAIN_PRECISION;
         uint64_t symbol4        = 300000 * GRAPHENE_BLOCKCHAIN_PRECISION;
         uint64_t long_symbol    = 5000   * GRAPHENE_BLOCKCHAIN_PRECISION;
         uint32_t price_per_kbyte = 10; /// only required for large memos.
      };

      asset                   fee;
      /// This account must sign and pay the fee for this operation. Later, this account may update the asset
      account_id_type         issuer;
      /// The ticker symbol of this asset
      string                  symbol;
      /// Number of digits to the right of decimal point, must be less than or equal to 12
      uint8_t                 precision = 0;

      /// Options common to all assets.
      ///
      /// @note
      /// common_options.core_exchange_rate technically needs to store the asset ID of this new asset. Since this
      /// ID is not known at the time this operation is created, create this price as though the new asset has
      /// instance ID 1, and the chain will overwrite it with the new asset's ID.
      asset_options              common_options;
      /// Options only available for BitAssets. MUST be non-null if and only if the asset is market-issued.
      optional<bitasset_options> bitasset_opts;
      /// For BitAssets, set this to true if the asset implements a prediction market; false otherwise
      bool is_prediction_market = false;
      extensions_type extensions;

      account_id_type fee_payer()const { return issuer; }
      void            validate()const;
      share_type      calculate_fee( const fee_params_t& k,
                                     const optional<uint64_t>& sub_asset_creation_fee )const;
   };

   /**
    *  @brief allows global settling of bitassets (black swan or prediction markets)
    *
    *  In order to use this operation, @ref asset_to_settle must have the global_settle flag set
    *
    *  When this operation is executed all balances are converted into the backing asset at the
    *  settle_price and all open margin positions are called at the settle price.  If this asset is
    *  used as backing for other bitassets, those bitassets will be force settled at their current
    *  feed price.
    */
   struct asset_global_settle_operation : public base_operation
   {
      struct fee_params_t { uint64_t fee = 500 * GRAPHENE_BLOCKCHAIN_PRECISION; };

      asset           fee;
      account_id_type issuer; ///< must equal issuer of @ref asset_to_settle
      asset_id_type   asset_to_settle;
      price           settle_price;
      extensions_type extensions;

      account_id_type fee_payer()const { return issuer; }
      void            validate()const;
   };

   /**
    * @brief Schedules a market-issued asset for automatic settlement
    * @ingroup operations
    *
    * Holders of market-issued assests may request a forced settlement for some amount of their asset.
    * This means that the specified sum will be locked by the chain and held for the settlement period,
    * after which time the chain will
    * choose a margin posision holder and buy the settled asset using the margin's collateral. The price of this sale
    * will be based on the feed price for the market-issued asset being settled.
    * The exact settlement price will be the
    * feed price at the time of settlement with an offset in favor of the margin position, where the offset is a
    * blockchain parameter set in the global_property_object.
    *
    * The fee is paid by @ref account, and @ref account must authorize this operation
    */
   struct asset_settle_operation : public base_operation
   {
      struct fee_params_t {
         /** this fee should be high to encourage small settlement requests to
          * be performed on the market rather than via forced settlement.
          *
          * Note that in the event of a black swan or prediction market close out
          * everyone will have to pay this fee.
          */
         uint64_t fee      = 100 * GRAPHENE_BLOCKCHAIN_PRECISION;
      };

      asset           fee;
      /// Account requesting the force settlement. This account pays the fee
      account_id_type account;
      /// Amount of asset to force settle. This must be a market-issued asset
      asset           amount;
      extensions_type extensions;

      account_id_type fee_payer()const { return account; }
      void            validate()const;
   };

   /**
    * Virtual op generated when force settlement is cancelled.
    */
   struct asset_settle_cancel_operation : public base_operation
   {
      struct fee_params_t { };

      asset_settle_cancel_operation() = default;
      asset_settle_cancel_operation( const force_settlement_id_type& fsid, const account_id_type& aid,
            const asset& a ) : settlement(fsid), account(aid), amount(a) {}

      asset           fee;
      force_settlement_id_type settlement;
      /// Account requesting the force settlement. This account pays the fee
      account_id_type account;
      /// Amount of asset to force settle. This must be a market-issued asset
      asset           amount;

      account_id_type fee_payer()const { return account; }
      /***
       * This is a virtual operation and should never be placed in a block
       * (i.e. in a proposal)
       */
      void validate() const { FC_ASSERT( !"Virtual operation"); }

      share_type calculate_fee(const fee_params_t& params)const
      { return 0; }
   };

   /**
    * @ingroup operations
    */
   struct asset_fund_fee_pool_operation : public base_operation
   {
      struct fee_params_t { uint64_t fee =  GRAPHENE_BLOCKCHAIN_PRECISION; };

      asset           fee; ///< core asset
      account_id_type from_account;
      asset_id_type   asset_id;
      share_type      amount; ///< core asset
      extensions_type extensions;

      account_id_type fee_payer()const { return from_account; }
      void       validate()const;
   };

   /**
    * @brief Update options common to all assets
    * @ingroup operations
    *
    * There are a number of options which all assets in the network use. These options are enumerated in the @ref
    * asset_options struct. This operation is used to update these options for an existing asset.
    *
    * @note This operation cannot be used to update BitAsset-specific options. For these options, use @ref
    * asset_update_bitasset_operation instead.
    *
    * @pre @ref issuer SHALL be an existing account and MUST match asset_object::issuer on @ref asset_to_update
    * @pre @ref fee SHALL be nonnegative, and @ref issuer MUST have a sufficient balance to pay it
    * @pre @ref new_options SHALL be internally consistent, as verified by @ref validate()
    * @post @ref asset_to_update will have options matching those of new_options
    */
   struct asset_update_operation : public base_operation
   {
      struct ext
      {
         /// After BSIP48, the precision of an asset can be updated if no supply is available
         /// @note The parties involved still need to be careful
         fc::optional<uint8_t> new_precision;
         /// After BSIP48, if this option is set to true, the asset's core_exchange_rate won't be updated.
         /// This is especially useful for committee-owned bitassets which can not be updated quickly.
         fc::optional<bool> skip_core_exchange_rate;
      };

      struct fee_params_t {
         uint64_t fee            = 500 * GRAPHENE_BLOCKCHAIN_PRECISION;
         uint32_t price_per_kbyte = 10;
      };

      asset_update_operation(){}

      asset           fee;
      account_id_type issuer;
      asset_id_type   asset_to_update;

      /// If the asset is to be given a new issuer, specify his ID here.
      optional<account_id_type>   new_issuer;
      asset_options               new_options;
      extension<ext>              extensions;

      account_id_type fee_payer()const { return issuer; }
      void            validate()const;
      share_type      calculate_fee(const fee_params_t& k)const;
   };

   /**
    * @brief Update options specific to BitAssets
    * @ingroup operations
    *
    * BitAssets have some options which are not relevant to other asset types. This operation is used to update those
    * options an an existing BitAsset.
    *
    * @pre @ref issuer MUST be an existing account and MUST match asset_object::issuer on @ref asset_to_update
    * @pre @ref asset_to_update MUST be a BitAsset, i.e. @ref graphene::chain::asset_object::is_market_issued()
    *                           returns true
    * @pre @ref fee MUST be nonnegative, and @ref issuer MUST have a sufficient balance to pay it
    * @pre @ref new_options SHALL be internally consistent, as verified by @ref validate()
    * @post @ref asset_to_update will have BitAsset-specific options matching those of new_options
    */
   struct asset_update_bitasset_operation : public base_operation
   {
      struct fee_params_t { uint64_t fee = 500 * GRAPHENE_BLOCKCHAIN_PRECISION; };

      asset           fee;
      account_id_type issuer;
      asset_id_type   asset_to_update;

      bitasset_options new_options;
      extensions_type  extensions;

      account_id_type fee_payer()const { return issuer; }
      void            validate()const;
   };

   /**
    * @brief Update the set of feed-producing accounts for a BitAsset
    * @ingroup operations
    *
    * BitAssets have price feeds selected by taking the median values of recommendations from a set of feed producers.
    * This operation is used to specify which accounts may produce feeds for a given BitAsset.
    *
    * @pre @ref issuer MUST be an existing account, and MUST match asset_object::issuer on @ref asset_to_update
    * @pre @ref issuer MUST NOT be the committee account
    * @pre @ref asset_to_update MUST be a BitAsset, i.e. @ref graphene::chain::asset_object::is_market_issued()
    *                           returns true
    * @pre @ref fee MUST be nonnegative, and @ref issuer MUST have a sufficient balance to pay it
    * @pre Cardinality of @ref new_feed_producers MUST NOT exceed @ref chain_parameters::maximum_asset_feed_publishers
    * @post @ref asset_to_update will have a set of feed producers matching @ref new_feed_producers
    * @post All valid feeds supplied by feed producers in @ref new_feed_producers, which were already feed producers
    * prior to execution of this operation, will be preserved
    */
   struct asset_update_feed_producers_operation : public base_operation
   {
      struct fee_params_t { uint64_t fee = 500 * GRAPHENE_BLOCKCHAIN_PRECISION; };

      asset           fee;
      account_id_type issuer;
      asset_id_type   asset_to_update;

      flat_set<account_id_type> new_feed_producers;
      extensions_type           extensions;

      account_id_type fee_payer()const { return issuer; }
      void            validate()const;
   };

   /**
    * @brief Publish price feeds for market-issued assets
    * @ingroup operations
    *
    * Price feed providers use this operation to publish their price feeds for market-issued assets. A price feed is
    * used to tune the market for a particular market-issued asset. For each value in the feed, the median across all
    * committee_member feeds for that asset is calculated and the market for the asset is configured with the median
    * of that value.
    *
    * The feed in the operation contains three prices: a call price limit, a short price limit,
    * and a settlement price.
    * The call limit price is structured as (collateral asset) / (debt asset) and the short limit price is structured
    * as (asset for sale) / (collateral asset). Note that the asset IDs are opposite to eachother, so if we're
    * publishing a feed for USD, the call limit price will be CORE/USD and the short limit price will be USD/CORE. The
    * settlement price may be flipped either direction, as long as it is a ratio between the market-issued asset and
    * its collateral.
    */
   struct asset_publish_feed_operation : public base_operation
   {
      struct ext
      {
         /// After BSIP77, price feed producers can feed ICR too
         fc::optional<uint16_t> initial_collateral_ratio;  // BSIP-77
      };

      struct fee_params_t { uint64_t fee = GRAPHENE_BLOCKCHAIN_PRECISION; };

      asset                  fee; ///< paid for by publisher
      account_id_type        publisher;
      asset_id_type          asset_id; ///< asset for which the feed is published
      price_feed             feed;
      extension<ext>         extensions;

      account_id_type fee_payer()const { return publisher; }
      void            validate()const;
   };

   /**
    * @ingroup operations
    */
   struct asset_issue_operation : public base_operation
   {
      struct fee_params_t {
         uint64_t fee = 20 * GRAPHENE_BLOCKCHAIN_PRECISION;
         uint32_t price_per_kbyte = GRAPHENE_BLOCKCHAIN_PRECISION;
      };

      asset            fee;
      account_id_type  issuer; ///< Must be asset_to_issue->asset_id->issuer
      asset            asset_to_issue;
      account_id_type  issue_to_account;


      /** user provided data encrypted to the memo key of the "to" account */
      optional<memo_data>  memo;
      extensions_type      extensions;

      account_id_type fee_payer()const { return issuer; }
      void            validate()const;
      share_type      calculate_fee(const fee_params_t& k)const;
   };

   /**
    * @brief used to take an asset out of circulation, returning to the issuer
    * @ingroup operations
    *
    * @note You cannot use this operation on market-issued assets.
    */
   struct asset_reserve_operation : public base_operation
   {
      struct fee_params_t { uint64_t fee = 20 * GRAPHENE_BLOCKCHAIN_PRECISION; };

      asset             fee;
      account_id_type   payer;
      asset             amount_to_reserve;
      extensions_type   extensions;

      account_id_type fee_payer()const { return payer; }
      void            validate()const;
   };

   /**
    * @brief used to transfer accumulated fees back to the issuer's balance.
    */
   struct asset_claim_fees_operation : public base_operation
   {
      struct fee_params_t {
         uint64_t fee = 20 * GRAPHENE_BLOCKCHAIN_PRECISION;
      };

      struct additional_options_type
      {
         /// Which asset to claim fees from. This is needed, e.g., to claim collateral-
         /// denominated fees from a collateral-backed smart asset. If unset, assumed to be same
         /// asset as amount_to_claim is denominated in, such as would be the case when claiming
         /// market fees. If set, validation requires it to be a different asset_id than
         /// amount_to_claim (else there would exist two ways to form the same request).
         fc::optional<asset_id_type> claim_from_asset_id;
      };

      asset           fee;
      account_id_type issuer; ///< must match issuer of asset from which we claim fees
      asset           amount_to_claim;

      extension<additional_options_type> extensions;

      account_id_type fee_payer()const { return issuer; }
      void            validate()const;
   };

   /**
    * @brief Update issuer of an asset
    * @ingroup operations
    *
    * An issuer has general administrative power of an asset and in some cases
    * also its shares issued to individuals. Thus, changing the issuer today
    * requires the use of a separate operation that needs to be signed by the
    * owner authority.
    *
    */
   struct asset_update_issuer_operation : public base_operation
   {
      struct fee_params_t {
         uint64_t fee            = 20 * GRAPHENE_BLOCKCHAIN_PRECISION;
      };

      asset           fee;
      account_id_type issuer;
      asset_id_type   asset_to_update;
      account_id_type new_issuer;
      extensions_type extensions;

      account_id_type fee_payer()const { return issuer; }
      void            validate()const;

      void get_required_owner_authorities( flat_set<account_id_type>& a )const
      { a.insert( issuer ); }

      void get_required_active_authorities( flat_set<account_id_type>& a )const
      {  }

   };

   /**
    * @brief Transfers ESH from the fee pool of a specified asset back to the issuer's balance

    * @param fee Payment for the operation execution
    * @param issuer Account which will be used for transfering ESH
    * @param asset_id Id of the asset whose fee pool is going to be drained
    * @param amount_to_claim Amount of ESH to claim from the fee pool
    * @param extensions Field for future expansion

    * @pre @ref fee must be paid in the asset other than the one whose pool is being drained
    * @pre @ref amount_to_claim should be specified in the core asset
    * @pre @ref amount_to_claim should be nonnegative
    */
   struct asset_claim_pool_operation : public base_operation
   {
      struct fee_params_t {
         uint64_t fee = 20 * GRAPHENE_BLOCKCHAIN_PRECISION;
      };

      asset           fee;
      account_id_type issuer;
      asset_id_type   asset_id;        /// fee.asset_id must != asset_id
      asset           amount_to_claim; /// core asset
      extensions_type extensions;

      account_id_type fee_payer()const { return issuer; }
      void            validate()const;
   };

} } // graphene::protocol

FC_REFLECT( graphene::protocol::asset_claim_fees_operation, (fee)(issuer)(amount_to_claim)(extensions) )
FC_REFLECT( graphene::protocol::asset_claim_fees_operation::fee_params_t, (fee) )
FC_REFLECT( graphene::protocol::asset_claim_fees_operation::additional_options_type, (claim_from_asset_id) )

FC_REFLECT( graphene::protocol::asset_claim_pool_operation, (fee)(issuer)(asset_id)(amount_to_claim)(extensions) )
FC_REFLECT( graphene::protocol::asset_claim_pool_operation::fee_params_t, (fee) )

FC_REFLECT( graphene::protocol::asset_options,
            (max_supply)
            (market_fee_percent)
            (max_market_fee)
            (issuer_permissions)
            (flags)
            (core_exchange_rate)
            (whitelist_authorities)
            (blacklist_authorities)
            (whitelist_markets)
            (blacklist_markets)
            (description)
            (extensions)
          )

FC_REFLECT( graphene::protocol::bitasset_options::ext,
            (initial_collateral_ratio)
            (maintenance_collateral_ratio)
            (maximum_short_squeeze_ratio)
            (margin_call_fee_ratio)
            (force_settle_fee_percent)
            (black_swan_response_method)
          )

FC_REFLECT( graphene::protocol::bitasset_options,
            (feed_lifetime_sec)
            (minimum_feeds)
            (force_settlement_delay_sec)
            (force_settlement_offset_percent)
            (maximum_force_settlement_volume)
            (short_backing_asset)
            (extensions)
          )

FC_REFLECT( graphene::protocol::additional_asset_options,
            (reward_percent)(whitelist_market_fee_sharing)(taker_fee_percent) )

FC_REFLECT( graphene::protocol::asset_update_operation::ext, (new_precision)(skip_core_exchange_rate) )
FC_REFLECT( graphene::protocol::asset_publish_feed_operation::ext, (initial_collateral_ratio) )

FC_REFLECT( graphene::protocol::asset_create_operation::fee_params_t,
            (symbol3)(symbol4)(long_symbol)(price_per_kbyte) )

FC_REFLECT( graphene::protocol::asset_global_settle_operation::fee_params_t, (fee) )
FC_REFLECT( graphene::protocol::asset_settle_operation::fee_params_t, (fee) )
FC_REFLECT( graphene::protocol::asset_settle_cancel_operation::fee_params_t, )
FC_REFLECT( graphene::protocol::asset_fund_fee_pool_operation::fee_params_t, (fee) )
FC_REFLECT( graphene::protocol::asset_update_operation::fee_params_t, (fee)(price_per_kbyte) )
FC_REFLECT( graphene::protocol::asset_update_issuer_operation::fee_params_t, (fee) )
FC_REFLECT( graphene::protocol::asset_update_bitasset_operation::fee_params_t, (fee) )
FC_REFLECT( graphene::protocol::asset_update_feed_producers_operation::fee_params_t, (fee) )
FC_REFLECT( graphene::protocol::asset_publish_feed_operation::fee_params_t, (fee) )
FC_REFLECT( graphene::protocol::asset_issue_operation::fee_params_t, (fee)(price_per_kbyte) )
FC_REFLECT( graphene::protocol::asset_reserve_operation::fee_params_t, (fee) )


FC_REFLECT( graphene::protocol::asset_create_operation,
            (fee)
            (issuer)
            (symbol)
            (precision)
            (common_options)
            (bitasset_opts)
            (is_prediction_market)
            (extensions)
          )
FC_REFLECT( graphene::protocol::asset_update_operation,
            (fee)
            (issuer)
            (asset_to_update)
            (new_issuer)
            (new_options)
            (extensions)
          )
FC_REFLECT( graphene::protocol::asset_update_issuer_operation,
            (fee)
            (issuer)
            (asset_to_update)
            (new_issuer)
            (extensions)
          )
FC_REFLECT( graphene::protocol::asset_update_bitasset_operation,
            (fee)
            (issuer)
            (asset_to_update)
            (new_options)
            (extensions)
          )
FC_REFLECT( graphene::protocol::asset_update_feed_producers_operation,
            (fee)(issuer)(asset_to_update)(new_feed_producers)(extensions)
          )
FC_REFLECT( graphene::protocol::asset_publish_feed_operation,
            (fee)(publisher)(asset_id)(feed)(extensions) )
FC_REFLECT( graphene::protocol::asset_settle_operation, (fee)(account)(amount)(extensions) )
FC_REFLECT( graphene::protocol::asset_settle_cancel_operation, (fee)(settlement)(account)(amount) )
FC_REFLECT( graphene::protocol::asset_global_settle_operation,
            (fee)(issuer)(asset_to_settle)(settle_price)(extensions) )
FC_REFLECT( graphene::protocol::asset_issue_operation,
            (fee)(issuer)(asset_to_issue)(issue_to_account)(memo)(extensions) )
FC_REFLECT( graphene::protocol::asset_reserve_operation,
            (fee)(payer)(amount_to_reserve)(extensions) )

FC_REFLECT( graphene::protocol::asset_fund_fee_pool_operation, (fee)(from_account)(asset_id)(amount)(extensions) )

GRAPHENE_DECLARE_EXTERNAL_SERIALIZATION( graphene::protocol::asset_options )
GRAPHENE_DECLARE_EXTERNAL_SERIALIZATION( graphene::protocol::bitasset_options::ext )
GRAPHENE_DECLARE_EXTERNAL_SERIALIZATION( graphene::protocol::bitasset_options )
GRAPHENE_DECLARE_EXTERNAL_SERIALIZATION( graphene::protocol::additional_asset_options )

GRAPHENE_DECLARE_EXTERNAL_SERIALIZATION( graphene::protocol::asset_update_operation::ext )
GRAPHENE_DECLARE_EXTERNAL_SERIALIZATION( graphene::protocol::asset_publish_feed_operation::ext )

GRAPHENE_DECLARE_EXTERNAL_SERIALIZATION( graphene::protocol::asset_create_operation::fee_params_t )
GRAPHENE_DECLARE_EXTERNAL_SERIALIZATION( graphene::protocol::asset_global_settle_operation::fee_params_t )
GRAPHENE_DECLARE_EXTERNAL_SERIALIZATION( graphene::protocol::asset_settle_operation::fee_params_t )
GRAPHENE_DECLARE_EXTERNAL_SERIALIZATION( graphene::protocol::asset_fund_fee_pool_operation::fee_params_t )
GRAPHENE_DECLARE_EXTERNAL_SERIALIZATION( graphene::protocol::asset_claim_pool_operation::fee_params_t )
GRAPHENE_DECLARE_EXTERNAL_SERIALIZATION( graphene::protocol::asset_claim_fees_operation::fee_params_t )
GRAPHENE_DECLARE_EXTERNAL_SERIALIZATION( graphene::protocol::asset_claim_fees_operation::additional_options_type )
GRAPHENE_DECLARE_EXTERNAL_SERIALIZATION( graphene::protocol::asset_update_operation::fee_params_t )
GRAPHENE_DECLARE_EXTERNAL_SERIALIZATION( graphene::protocol::asset_update_issuer_operation::fee_params_t )
GRAPHENE_DECLARE_EXTERNAL_SERIALIZATION( graphene::protocol::asset_update_bitasset_operation::fee_params_t )
GRAPHENE_DECLARE_EXTERNAL_SERIALIZATION(
   graphene::protocol::asset_update_feed_producers_operation::fee_params_t )
GRAPHENE_DECLARE_EXTERNAL_SERIALIZATION( graphene::protocol::asset_publish_feed_operation::fee_params_t )
GRAPHENE_DECLARE_EXTERNAL_SERIALIZATION( graphene::protocol::asset_issue_operation::fee_params_t )
GRAPHENE_DECLARE_EXTERNAL_SERIALIZATION( graphene::protocol::asset_reserve_operation::fee_params_t )

GRAPHENE_DECLARE_EXTERNAL_SERIALIZATION( graphene::protocol::asset_create_operation )
GRAPHENE_DECLARE_EXTERNAL_SERIALIZATION( graphene::protocol::asset_global_settle_operation )
GRAPHENE_DECLARE_EXTERNAL_SERIALIZATION( graphene::protocol::asset_settle_operation )
GRAPHENE_DECLARE_EXTERNAL_SERIALIZATION( graphene::protocol::asset_settle_cancel_operation )
GRAPHENE_DECLARE_EXTERNAL_SERIALIZATION( graphene::protocol::asset_fund_fee_pool_operation )
GRAPHENE_DECLARE_EXTERNAL_SERIALIZATION( graphene::protocol::asset_claim_pool_operation )
GRAPHENE_DECLARE_EXTERNAL_SERIALIZATION( graphene::protocol::asset_claim_fees_operation )
GRAPHENE_DECLARE_EXTERNAL_SERIALIZATION( graphene::protocol::asset_update_operation )
GRAPHENE_DECLARE_EXTERNAL_SERIALIZATION( graphene::protocol::asset_update_issuer_operation )
GRAPHENE_DECLARE_EXTERNAL_SERIALIZATION( graphene::protocol::asset_update_bitasset_operation )
GRAPHENE_DECLARE_EXTERNAL_SERIALIZATION( graphene::protocol::asset_update_feed_producers_operation )
GRAPHENE_DECLARE_EXTERNAL_SERIALIZATION( graphene::protocol::asset_publish_feed_operation )
GRAPHENE_DECLARE_EXTERNAL_SERIALIZATION( graphene::protocol::asset_issue_operation )
GRAPHENE_DECLARE_EXTERNAL_SERIALIZATION( graphene::protocol::asset_reserve_operation )
