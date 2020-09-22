// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <src/connectivity/wlan/lib/common/cpp/include/wlan/common/element.h>
#include <wlan/common/channel.h>
#include <wlan/common/element_splitter.h>
#include <wlan/common/parse_element.h>
#include <wlan/mlme/parse_beacon.h>
#include <wlan/mlme/wlan.h>  // for to_enum_type

namespace wlan {

namespace wlan_mlme = ::fuchsia::wlan::mlme;

std::optional<wlan_channel_bandwidth_t> GetVhtCbw(const VhtOperation& vht_op) {
  switch (vht_op.vht_cbw) {
    case to_enum_type(VhtOperation::VhtChannelBandwidth::VHT_CBW_80_160_80P80): {
      // See IEEE Std 802.11-2016, Table 9-253
      auto seg0 = vht_op.center_freq_seg0;
      auto seg1 = vht_op.center_freq_seg1;
      auto gap = (seg0 >= seg1) ? (seg0 - seg1) : (seg1 - seg0);

      if (seg1 > 0 && gap < 8) {
        // Reserved case. Fallback to HT CBW
      } else if (seg1 > 0 && (gap > 8 && gap <= 16)) {
        // Reserved case. Fallback to HT CBW
      } else if (seg1 == 0) {
        return {WLAN_CHANNEL_BANDWIDTH__80};
      } else if (gap == 8) {
        return {WLAN_CHANNEL_BANDWIDTH__160};
      } else if (gap > 16) {
        return {WLAN_CHANNEL_BANDWIDTH__80P80};
      }
    }
    default:
      break;
  }
  return {};
}

wlan_channel_t DeriveChannel(uint8_t rx_channel, std::optional<uint8_t> dsss_chan,
                             const HtOperation* ht_op,
                             std::optional<wlan_channel_bandwidth_t> vht_cbw) {
  wlan_channel_t chan = {
      .primary = dsss_chan.value_or(rx_channel),
      .cbw = WLAN_CHANNEL_BANDWIDTH__20,  // default
      .secondary80 = 0,
  };

  // See IEEE 802.11-2016, Table 9-250, Table 11-24.

  if (ht_op == nullptr) {
    // No HT or VHT support. Even if there was attached an incomplete set of
    // HT/VHT IEs, those are not be properly decodable.
    return chan;
  }

  chan.primary = ht_op->primary_chan;

  switch (ht_op->head.secondary_chan_offset()) {
    case to_enum_type(HtOpInfoHead::SecChanOffset::SECONDARY_ABOVE):
      chan.cbw = WLAN_CHANNEL_BANDWIDTH__40ABOVE;
      break;
    case to_enum_type(HtOpInfoHead::SecChanOffset::SECONDARY_BELOW):
      chan.cbw = WLAN_CHANNEL_BANDWIDTH__40BELOW;
      break;
    default:  // SECONDARY_NONE or RESERVED
      chan.cbw = WLAN_CHANNEL_BANDWIDTH__20;
      break;
  }

  // This overrides Secondary Channel Offset.
  // TODO(fxbug.dev/29392): Conditionally apply
  if (ht_op->head.sta_chan_width() == to_enum_type(HtOpInfoHead::StaChanWidth::TWENTY)) {
    chan.cbw = WLAN_CHANNEL_BANDWIDTH__20;
    return chan;
  }

  if (vht_cbw) {
    chan.cbw = *vht_cbw;
  }
  return chan;
}

static bool IsBlankSsid(fbl::Span<const uint8_t> ssid) {
  return std::all_of(ssid.cbegin(), ssid.cend(), [](auto c) { return c == '\0'; });
}

static void DoParseBeaconElements(fbl::Span<const uint8_t> ies, uint8_t rx_channel,
                                  wlan_mlme::BSSDescription* bss_desc,
                                  std::optional<uint8_t>* dsss_chan,
                                  fbl::Span<const SupportedRate>* supp_rates,
                                  fbl::Span<const SupportedRate>* ext_supp_rates) {
  for (auto [id, raw_body] : common::ElementSplitter(ies)) {
    switch (id) {
      case element_id::kSsid:
        if (auto ssid = common::ParseSsid(raw_body)) {
          // Don't update if SSID for BSS description is already populated and
          // the SSID received from the beacon is one that's blanked out (SSID
          // is empty, or full of 0 bytes). This can happen if we receive a
          // probe response from a hidden AP (which shows the SSID), and then
          // receive a beacon from the same AP (which blanks out the SSID).
          if (!bss_desc->ssid.empty() && IsBlankSsid(*ssid)) {
            continue;
          }
          bss_desc->ssid.assign(ssid->begin(), ssid->end());
        }
        break;
      case element_id::kSuppRates:
        if (auto rates = common::ParseSupportedRates(raw_body)) {
          *supp_rates = *rates;
        }
        break;
      case element_id::kExtSuppRates:
        if (auto rates = common::ParseExtendedSupportedRates(raw_body)) {
          *ext_supp_rates = *rates;
        }
        break;
      case element_id::kDsssParamSet:
        if (auto dsss = common::ParseDsssParamSet(raw_body)) {
          *dsss_chan = {dsss->current_chan};
        }
        break;
      case element_id::kCountry:
        if (auto c = common::ParseCountry(raw_body)) {
          bss_desc->country.emplace();
          bss_desc->country->assign(c->country.data, c->country.data + Country::kCountryLen);
          // TODO(porce): Handle Subband Triplet Sequence field.
        }
        break;
      case element_id::kRsn: {
        bss_desc->rsne.emplace();
        bss_desc->rsne->resize(sizeof(ElementHeader));
        auto header = reinterpret_cast<ElementHeader*>(bss_desc->rsne->data());
        header->id = static_cast<uint8_t>(element_id::kRsn);
        header->len = raw_body.size();
        bss_desc->rsne->insert(bss_desc->rsne->end(), raw_body.begin(), raw_body.end());
        break;
      }
      case element_id::kHtCapabilities:
        if (auto ht_cap = common::ParseHtCapabilities(raw_body)) {
          bss_desc->ht_cap = wlan_mlme::HtCapabilities::New();
          static_assert(sizeof(bss_desc->ht_cap->bytes) == sizeof(*ht_cap));
          memcpy(bss_desc->ht_cap->bytes.data(), ht_cap, sizeof(*ht_cap));
        }
        break;
      case element_id::kHtOperation:
        if (auto ht_op = common::ParseHtOperation(raw_body)) {
          bss_desc->ht_op = wlan_mlme::HtOperation::New();
          static_assert(sizeof(bss_desc->ht_op->bytes) == sizeof(*ht_op));
          memcpy(bss_desc->ht_op->bytes.data(), ht_op, sizeof(*ht_op));
        }
        break;
      case element_id::kVhtCapabilities:
        if (auto vht_cap = common::ParseVhtCapabilities(raw_body)) {
          bss_desc->vht_cap = wlan_mlme::VhtCapabilities::New();
          static_assert(sizeof(bss_desc->vht_cap->bytes) == sizeof(*vht_cap));
          memcpy(bss_desc->vht_cap->bytes.data(), vht_cap, sizeof(*vht_cap));
        }
        break;
      case element_id::kVhtOperation:
        if (auto vht_op = common::ParseVhtOperation(raw_body)) {
          bss_desc->vht_op = wlan_mlme::VhtOperation::New();
          static_assert(sizeof(bss_desc->vht_op->bytes) == sizeof(*vht_op));
          memcpy(bss_desc->vht_op->bytes.data(), vht_op, sizeof(*vht_op));
        }
        break;
      default:
        break;
    }
  }
}

void FillRates(fbl::Span<const SupportedRate> supp_rates,
               fbl::Span<const SupportedRate> ext_supp_rates, ::std::vector<uint8_t>* rates) {
  rates->assign(supp_rates.cbegin(), supp_rates.cend());
  rates->insert(rates->end(), ext_supp_rates.cbegin(), ext_supp_rates.cend());
  if (rates->size() > wlan_mlme::RATES_MAX_LEN) {
    rates->resize(wlan_mlme::RATES_MAX_LEN);
  }
}

void ParseBeaconElements(fbl::Span<const uint8_t> ies, uint8_t rx_channel,
                         wlan_mlme::BSSDescription* bss_desc) {
  std::optional<uint8_t> dsss_chan{};
  fbl::Span<const SupportedRate> supp_rates;
  fbl::Span<const SupportedRate> ext_supp_rates;

  DoParseBeaconElements(ies, rx_channel, bss_desc, &dsss_chan, &supp_rates, &ext_supp_rates);
  FillRates(supp_rates, ext_supp_rates, &bss_desc->rates);

  std::optional<uint8_t> vht_cbw{};
  if (bss_desc->vht_op) {
    static_assert(sizeof(bss_desc->vht_op->bytes) == sizeof(VhtOperation));
    const auto& vht_op = *common::ParseVhtOperation(bss_desc->vht_op->bytes);
    vht_cbw = GetVhtCbw(vht_op);
  }
  auto chan = DeriveChannel(rx_channel, dsss_chan, common::ParseHtOperation(bss_desc->ht_op->bytes),
                            vht_cbw);
  bss_desc->chan = common::ToFidl(chan);
}

}  // namespace wlan
