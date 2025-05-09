/*
 * Copyright (c) 2015-2018 Cryptonomex, Inc., and contributors.
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
#include <graphene/protocol/asset_ops.hpp>

#include <fc/io/raw.hpp>

#include <locale>

namespace graphene { namespace protocol {

/**
 *  Valid symbols can contain [A-Z0-9], and '.'
 *  They must start with [A, Z]
 *  They must end with [A, Z] before HF_620 or [A-Z0-9] after it
 *  They can contain a maximum of one '.'
 */
bool is_valid_symbol( const string& symbol )
{
    static const std::locale& loc = std::locale::classic();
    if( symbol.size() < GRAPHENE_MIN_ASSET_SYMBOL_LENGTH )
        return false;

    // if( symbol.substr(0,3) == "BIT" ) // see HARDFORK_TEST_201706_TIME
    //    return false;

    if( symbol.size() > GRAPHENE_MAX_ASSET_SYMBOL_LENGTH )
        return false;

    if( !isalpha( symbol.front(), loc ) )
        return false;

    if( !isalnum( symbol.back(), loc ) )
        return false;

    bool dot_already_present = false;
    for( const auto c : symbol )
    {
        if( (isalpha( c, loc ) && isupper( c, loc )) || isdigit( c, loc ) )
            continue;

        if( c == '.' )
        {
            if( dot_already_present )
                return false;

            dot_already_present = true;
            continue;
        }

        return false;
    }

    return true;
}

share_type asset_issue_operation::calculate_fee(const fee_params_t& k)const
{
   return k.fee + calculate_data_fee( fc::raw::pack_size(memo), k.price_per_kbyte );
}

share_type asset_create_operation::calculate_fee( const asset_create_operation::fee_params_t& param,
                                                  const optional<uint64_t>& sub_asset_creation_fee )const
{
   share_type core_fee_required = param.long_symbol;

   if( sub_asset_creation_fee.valid() && symbol.find('.') != std::string::npos )
   {
      core_fee_required = *sub_asset_creation_fee;
   }
   else
   {
      switch( symbol.size() )
      {
      case 3: core_fee_required = param.symbol3;
          break;
      case 4: core_fee_required = param.symbol4;
          break;
      default:
          break;
      }
   }

   // common_options contains several lists and a string. Charge fees for its size
   core_fee_required += calculate_data_fee( fc::raw::pack_size(*this), param.price_per_kbyte );

   return core_fee_required;
}

void  asset_create_operation::validate()const
{
   FC_ASSERT( fee.amount >= 0 );
   FC_ASSERT( is_valid_symbol(symbol) );
   common_options.validate();
   // TODO fix the missing check for witness_fed_asset and committee_fed_asset with a hard fork
   if( 0 != ( common_options.issuer_permissions & NON_UIA_ONLY_ISSUER_PERMISSION_MASK
              & (uint16_t)( ~(witness_fed_asset|committee_fed_asset) ) ) )
      FC_ASSERT( bitasset_opts.valid() );
   if( is_prediction_market )
   {
      FC_ASSERT( bitasset_opts.valid(), "Cannot have a User-Issued Asset implement a prediction market." );
      FC_ASSERT( 0 != (common_options.issuer_permissions & global_settle) );
      FC_ASSERT( 0 == (common_options.issuer_permissions & disable_bsrm_update) );
      FC_ASSERT( !bitasset_opts->extensions.value.black_swan_response_method.valid(),
                 "Can not set black_swan_response_method for Prediction Markets" );
   }
   if( bitasset_opts ) bitasset_opts->validate();

   asset dummy = asset(1) * common_options.core_exchange_rate;
   FC_ASSERT(dummy.asset_id == asset_id_type(1));
   FC_ASSERT(precision <= 12);
}

void asset_update_operation::validate()const
{
   FC_ASSERT( fee.amount >= 0 );
   if( new_issuer )
      FC_ASSERT(issuer != *new_issuer);
   new_options.validate();

   asset dummy = asset(1, asset_to_update) * new_options.core_exchange_rate;
   FC_ASSERT(dummy.asset_id == asset_id_type());

   if( extensions.value.new_precision.valid() )
      FC_ASSERT( *extensions.value.new_precision <= 12 );

   if( extensions.value.skip_core_exchange_rate.valid() )
   {
      FC_ASSERT( *extensions.value.skip_core_exchange_rate == true,
                 "If skip_core_exchange_rate is specified, it can only be true" );
   }
}

void asset_update_issuer_operation::validate()const
{
   FC_ASSERT( fee.amount >= 0 );
   FC_ASSERT( issuer != new_issuer );
}

share_type asset_update_operation::calculate_fee(const asset_update_operation::fee_params_t& k)const
{
   return k.fee + calculate_data_fee( fc::raw::pack_size(*this), k.price_per_kbyte );
}


void asset_publish_feed_operation::validate()const
{
   FC_ASSERT( fee.amount >= 0 );
   feed.validate();

   // maybe some of these could be moved to feed.validate()
   if( !feed.core_exchange_rate.is_null() )
   {
      feed.core_exchange_rate.validate();
   }
   if( (!feed.settlement_price.is_null()) && (!feed.core_exchange_rate.is_null()) )
   {
      FC_ASSERT( feed.settlement_price.base.asset_id == feed.core_exchange_rate.base.asset_id );
   }

   FC_ASSERT( !feed.settlement_price.is_null() );
   FC_ASSERT( !feed.core_exchange_rate.is_null() );
   FC_ASSERT( feed.is_for( asset_id ) );

   if( extensions.value.initial_collateral_ratio.valid() )
   {
      FC_ASSERT( *extensions.value.initial_collateral_ratio >= GRAPHENE_MIN_COLLATERAL_RATIO );
      FC_ASSERT( *extensions.value.initial_collateral_ratio <= GRAPHENE_MAX_COLLATERAL_RATIO );
   }
}

void asset_reserve_operation::validate()const
{
   FC_ASSERT( fee.amount >= 0 );
   FC_ASSERT( amount_to_reserve.amount.value <= GRAPHENE_MAX_SHARE_SUPPLY );
   FC_ASSERT( amount_to_reserve.amount.value > 0 );
}

void asset_issue_operation::validate()const
{
   FC_ASSERT( fee.amount >= 0 );
   FC_ASSERT( asset_to_issue.amount.value <= GRAPHENE_MAX_SHARE_SUPPLY );
   FC_ASSERT( asset_to_issue.amount.value > 0 );
   FC_ASSERT( asset_to_issue.asset_id != asset_id_type(0) );
}

void asset_fund_fee_pool_operation::validate() const
{
   FC_ASSERT( fee.amount >= 0 );
   FC_ASSERT( fee.asset_id == asset_id_type() );
   FC_ASSERT( amount > 0 );
}

void asset_settle_operation::validate() const
{
   FC_ASSERT( fee.amount >= 0 );
   FC_ASSERT( amount.amount >= 0 );
}

void asset_update_bitasset_operation::validate() const
{
   FC_ASSERT( fee.amount >= 0 );
   new_options.validate();
}

void asset_update_feed_producers_operation::validate() const
{
   FC_ASSERT( fee.amount >= 0 );
}

void asset_global_settle_operation::validate()const
{
   FC_ASSERT( fee.amount >= 0 );
   FC_ASSERT( asset_to_settle == settle_price.base.asset_id );
}

void bitasset_options::validate() const
{
   FC_ASSERT(minimum_feeds > 0);
   FC_ASSERT(force_settlement_offset_percent <= GRAPHENE_100_PERCENT);
   FC_ASSERT(maximum_force_settlement_volume <= GRAPHENE_100_PERCENT);

   if( extensions.value.margin_call_fee_ratio.valid() )
      FC_ASSERT( *extensions.value.margin_call_fee_ratio <= GRAPHENE_MAX_COLLATERAL_RATIO );

   if( extensions.value.initial_collateral_ratio.valid() )
   {
      FC_ASSERT( *extensions.value.initial_collateral_ratio >= GRAPHENE_MIN_COLLATERAL_RATIO );
      FC_ASSERT( *extensions.value.initial_collateral_ratio <= GRAPHENE_MAX_COLLATERAL_RATIO );
   }
   if( extensions.value.maintenance_collateral_ratio.valid() )
   {
      FC_ASSERT( *extensions.value.maintenance_collateral_ratio >= GRAPHENE_MIN_COLLATERAL_RATIO );
      FC_ASSERT( *extensions.value.maintenance_collateral_ratio <= GRAPHENE_MAX_COLLATERAL_RATIO );
   }
   if( extensions.value.maximum_short_squeeze_ratio.valid() )
   {
      FC_ASSERT( *extensions.value.maximum_short_squeeze_ratio >= GRAPHENE_MIN_COLLATERAL_RATIO );
      FC_ASSERT( *extensions.value.maximum_short_squeeze_ratio <= GRAPHENE_MAX_COLLATERAL_RATIO );
   }

   if( extensions.value.force_settle_fee_percent.valid() )
      FC_ASSERT( *extensions.value.force_settle_fee_percent <= GRAPHENE_100_PERCENT );

   if( extensions.value.black_swan_response_method.valid() )
   {
      auto bsrm_count = static_cast<uint8_t>( black_swan_response_type::BSRM_TYPE_COUNT );
      FC_ASSERT( *extensions.value.black_swan_response_method < bsrm_count,
                 "black_swan_response_method should be less than ${c}", ("c",bsrm_count) );
   }
}

void asset_options::validate()const
{
   FC_ASSERT( max_supply > 0 );
   FC_ASSERT( max_supply <= GRAPHENE_MAX_SHARE_SUPPLY );
   // The non-negative maker fee must be less than or equal to 100%
   FC_ASSERT( market_fee_percent <= GRAPHENE_100_PERCENT );

   // The non-negative taker fee must be less than or equal to 100%
   if( extensions.value.taker_fee_percent.valid() )
      FC_ASSERT( *extensions.value.taker_fee_percent <= GRAPHENE_100_PERCENT );

   FC_ASSERT( max_market_fee >= 0 && max_market_fee <= GRAPHENE_MAX_SHARE_SUPPLY );
   // There must be no high bits in permissions whose meaning is not known.
   FC_ASSERT( 0 == (issuer_permissions & (uint16_t)(~ASSET_ISSUER_PERMISSION_MASK)) );
   // The permission-only bits can not be set in flag
   FC_ASSERT( 0 == (flags & global_settle),
              "Can not set global_settle flag, it is for issuer permission only" );

   // the witness_fed and committee_fed flags cannot be set simultaneously
   FC_ASSERT( (flags & (witness_fed_asset | committee_fed_asset)) != (witness_fed_asset | committee_fed_asset) );
   core_exchange_rate.validate();
   FC_ASSERT( core_exchange_rate.base.asset_id.instance.value == 0 ||
              core_exchange_rate.quote.asset_id.instance.value == 0 );

   if(!whitelist_authorities.empty() || !blacklist_authorities.empty())
      FC_ASSERT( 0 != (flags & white_list) );
   for( auto item : whitelist_markets )
   {
      FC_ASSERT( blacklist_markets.find(item) == blacklist_markets.end() );
   }
   for( auto item : blacklist_markets )
   {
      FC_ASSERT( whitelist_markets.find(item) == whitelist_markets.end() );
   }
   if( extensions.value.reward_percent.valid() )
      FC_ASSERT( *extensions.value.reward_percent <= GRAPHENE_100_PERCENT );
}

// Note: this function is only called after the BSIP 48/75 hardfork
void asset_options::validate_flags( bool is_market_issued, bool allow_disable_collateral_bid )const
{
   FC_ASSERT( 0 == (flags & (uint16_t)(~ASSET_ISSUER_PERMISSION_MASK)),
              "Can not set an unknown bit in flags" );
   if( !allow_disable_collateral_bid ) // before core-2281 hf, can not set the disable_collateral_bidding bit
      FC_ASSERT( 0 == (flags & disable_collateral_bidding),
                 "Can not set the 'disable_collateral_bidding' bit in flags between the core-2281 hardfork "
                 "and the BSIP_48_75 hardfork" );
   // Note: global_settle is checked in validate(), so do not check again here
   FC_ASSERT( 0 == (flags & disable_mcr_update),
              "Can not set disable_mcr_update flag, it is for issuer permission only" );
   FC_ASSERT( 0 == (flags & disable_icr_update),
              "Can not set disable_icr_update flag, it is for issuer permission only" );
   FC_ASSERT( 0 == (flags & disable_mssr_update),
              "Can not set disable_mssr_update flag, it is for issuer permission only" );
   FC_ASSERT( 0 == (flags & disable_bsrm_update),
              "Can not set disable_bsrm_update flag, it is for issuer permission only" );
   if( !is_market_issued )
   {
      FC_ASSERT( 0 == (flags & (uint16_t)(~UIA_ASSET_ISSUER_PERMISSION_MASK)),
                 "Can not set a flag for bitassets only to UIA" );
   }
}

uint16_t asset_options::get_enabled_issuer_permissions_mask() const
{
   return ( (issuer_permissions & ASSET_ISSUER_PERMISSION_ENABLE_BITS_MASK)
          | ((uint16_t)(~issuer_permissions) & ASSET_ISSUER_PERMISSION_DISABLE_BITS_MASK) );
}

void asset_claim_fees_operation::validate()const {
   FC_ASSERT( fee.amount >= 0 );
   FC_ASSERT( amount_to_claim.amount > 0 );
   if( extensions.value.claim_from_asset_id.valid() )
     FC_ASSERT( *extensions.value.claim_from_asset_id != amount_to_claim.asset_id );
}

void asset_claim_pool_operation::validate()const {
   FC_ASSERT( fee.amount >= 0 );
   FC_ASSERT( fee.asset_id != asset_id);
   FC_ASSERT( amount_to_claim.amount > 0 );
   FC_ASSERT( amount_to_claim.asset_id == asset_id_type());
}

} } // namespace graphene::protocol

GRAPHENE_IMPLEMENT_EXTERNAL_SERIALIZATION( graphene::protocol::asset_options )
GRAPHENE_IMPLEMENT_EXTERNAL_SERIALIZATION( graphene::protocol::bitasset_options::ext )
GRAPHENE_IMPLEMENT_EXTERNAL_SERIALIZATION( graphene::protocol::bitasset_options )
GRAPHENE_IMPLEMENT_EXTERNAL_SERIALIZATION( graphene::protocol::additional_asset_options )

GRAPHENE_IMPLEMENT_EXTERNAL_SERIALIZATION( graphene::protocol::asset_update_operation::ext )
GRAPHENE_IMPLEMENT_EXTERNAL_SERIALIZATION( graphene::protocol::asset_publish_feed_operation::ext )

GRAPHENE_IMPLEMENT_EXTERNAL_SERIALIZATION( graphene::protocol::asset_create_operation::fee_params_t )
GRAPHENE_IMPLEMENT_EXTERNAL_SERIALIZATION( graphene::protocol::asset_global_settle_operation::fee_params_t )
GRAPHENE_IMPLEMENT_EXTERNAL_SERIALIZATION( graphene::protocol::asset_settle_operation::fee_params_t )
GRAPHENE_IMPLEMENT_EXTERNAL_SERIALIZATION( graphene::protocol::asset_fund_fee_pool_operation::fee_params_t )
GRAPHENE_IMPLEMENT_EXTERNAL_SERIALIZATION( graphene::protocol::asset_claim_pool_operation::fee_params_t )
GRAPHENE_IMPLEMENT_EXTERNAL_SERIALIZATION( graphene::protocol::asset_claim_fees_operation::fee_params_t )
GRAPHENE_IMPLEMENT_EXTERNAL_SERIALIZATION( graphene::protocol::asset_claim_fees_operation::additional_options_type )
GRAPHENE_IMPLEMENT_EXTERNAL_SERIALIZATION( graphene::protocol::asset_update_operation::fee_params_t )
GRAPHENE_IMPLEMENT_EXTERNAL_SERIALIZATION( graphene::protocol::asset_update_issuer_operation::fee_params_t )
GRAPHENE_IMPLEMENT_EXTERNAL_SERIALIZATION( graphene::protocol::asset_update_bitasset_operation::fee_params_t )
GRAPHENE_IMPLEMENT_EXTERNAL_SERIALIZATION( graphene::protocol::asset_update_feed_producers_operation::fee_params_t )
GRAPHENE_IMPLEMENT_EXTERNAL_SERIALIZATION( graphene::protocol::asset_publish_feed_operation::fee_params_t )
GRAPHENE_IMPLEMENT_EXTERNAL_SERIALIZATION( graphene::protocol::asset_issue_operation::fee_params_t )
GRAPHENE_IMPLEMENT_EXTERNAL_SERIALIZATION( graphene::protocol::asset_reserve_operation::fee_params_t )

GRAPHENE_IMPLEMENT_EXTERNAL_SERIALIZATION( graphene::protocol::asset_create_operation )
GRAPHENE_IMPLEMENT_EXTERNAL_SERIALIZATION( graphene::protocol::asset_global_settle_operation )
GRAPHENE_IMPLEMENT_EXTERNAL_SERIALIZATION( graphene::protocol::asset_settle_operation )
GRAPHENE_IMPLEMENT_EXTERNAL_SERIALIZATION( graphene::protocol::asset_settle_cancel_operation )
GRAPHENE_IMPLEMENT_EXTERNAL_SERIALIZATION( graphene::protocol::asset_fund_fee_pool_operation )
GRAPHENE_IMPLEMENT_EXTERNAL_SERIALIZATION( graphene::protocol::asset_claim_pool_operation )
GRAPHENE_IMPLEMENT_EXTERNAL_SERIALIZATION( graphene::protocol::asset_claim_fees_operation )
GRAPHENE_IMPLEMENT_EXTERNAL_SERIALIZATION( graphene::protocol::asset_update_operation )
GRAPHENE_IMPLEMENT_EXTERNAL_SERIALIZATION( graphene::protocol::asset_update_issuer_operation )
GRAPHENE_IMPLEMENT_EXTERNAL_SERIALIZATION( graphene::protocol::asset_update_bitasset_operation )
GRAPHENE_IMPLEMENT_EXTERNAL_SERIALIZATION( graphene::protocol::asset_update_feed_producers_operation )
GRAPHENE_IMPLEMENT_EXTERNAL_SERIALIZATION( graphene::protocol::asset_publish_feed_operation )
GRAPHENE_IMPLEMENT_EXTERNAL_SERIALIZATION( graphene::protocol::asset_issue_operation )
GRAPHENE_IMPLEMENT_EXTERNAL_SERIALIZATION( graphene::protocol::asset_reserve_operation )
