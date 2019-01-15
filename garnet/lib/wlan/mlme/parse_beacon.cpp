// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <wlan/mlme/parse_beacon.h>

#include <wlan/common/channel.h>
#include <wlan/common/element_splitter.h>
#include <wlan/common/parse_element.h>
#include <wlan/mlme/wlan.h>  // for to_enum_type

namespace wlan {

namespace wlan_mlme = ::fuchsia::wlan::mlme;

std::optional<CBW> GetVhtCbw(const wlan_mlme::VhtOperation& vht_op) {
    switch (vht_op.vht_cbw) {
    case to_enum_type(wlan_mlme::VhtCbw::CBW_80_160_80P80): {
        // See IEEE Std 802.11-2016, Table 9-253
        auto seg0 = vht_op.center_freq_seg0;
        auto seg1 = vht_op.center_freq_seg1;
        auto gap = (seg0 >= seg1) ? (seg0 - seg1) : (seg1 - seg0);

        if (seg1 > 0 && gap < 8) {
            // Reserved case. Fallback to HT CBW
        } else if (seg1 > 0 && (gap > 8 && gap <= 16)) {
            // Reserved case. Fallback to HT CBW
        } else if (seg1 == 0) {
            return {CBW80};
        } else if (gap == 8) {
            return {CBW160};
        } else if (gap > 16) {
            return {CBW80P80};
        }
    }
    default:
        break;
    }
    return {};
}

wlan_channel_t DeriveChannel(uint8_t rx_channel, std::optional<uint8_t> dsss_chan,
                             const wlan_mlme::HtOperation* ht_op, std::optional<CBW> vht_cbw) {
    wlan_channel_t chan = {
        .primary = dsss_chan.value_or(rx_channel),
        .cbw = CBW20,  // default
        .secondary80 = 0,
    };

    // See IEEE 802.11-2016, Table 9-250, Table 11-24.

    if (ht_op == nullptr) {
        // No HT or VHT support. Even if there was attached an incomplete set of
        // HT/VHT IEs, those are not be properly decodable.
        return chan;
    }

    chan.primary = ht_op->primary_chan;

    switch (ht_op->ht_op_info.secondary_chan_offset) {
    case to_enum_type(wlan_mlme::SecChanOffset::SECONDARY_ABOVE):
        chan.cbw = CBW40ABOVE;
        break;
    case to_enum_type(wlan_mlme::SecChanOffset::SECONDARY_BELOW):
        chan.cbw = CBW40BELOW;
        break;
    default:  // SECONDARY_NONE or RESERVED
        chan.cbw = CBW20;
        break;
    }

    // This overrides Secondary Channel Offset.
    // TODO(NET-677): Conditionally apply
    if (ht_op->ht_op_info.sta_chan_width == to_enum_type(wlan_mlme::StaChanWidth::TWENTY)) {
        chan.cbw = CBW20;
        return chan;
    }

    if (vht_cbw) { chan.cbw = *vht_cbw; }
    return chan;
}

static bool IsBlankSsid(Span<const uint8_t> ssid) {
    return std::all_of(ssid.cbegin(), ssid.cend(), [](auto c) { return c == '\0'; });
}

static void DoParseBeaconElements(Span<const uint8_t> ies, uint8_t rx_channel,
                                  wlan_mlme::BSSDescription* bss_desc,
                                  std::optional<uint8_t>* dsss_chan,
                                  Span<const SupportedRate>* supp_rates,
                                  Span<const SupportedRate>* ext_supp_rates) {
    for (auto [id, raw_body] : common::ElementSplitter(ies)) {
        switch (id) {
        case element_id::kSsid:
            if (auto ssid = common::ParseSsid(raw_body)) {
                // Don't update if SSID for BSS description is already populated and the SSID
                // received from the beacon is one that's blanked out (SSID is empty, or full of 0
                // bytes). This can happen if we receive a probe response from a hidden AP (which
                // shows the SSID), and then receive a beacon from the same AP (which blanks out
                // the SSID).
                if (!bss_desc->ssid.empty() && IsBlankSsid(*ssid)) { continue; }
                bss_desc->ssid.assign(ssid->begin(), ssid->end());
            }
            break;
        case element_id::kSuppRates:
            if (auto rates = common::ParseSupportedRates(raw_body)) { *supp_rates = *rates; }
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
                bss_desc->country.resize(0);
                bss_desc->country->assign(c->country.data, c->country.data + Country::kCountryLen);
                // TODO(porce): Handle Subband Triplet Sequence field.
            }
            break;
        case element_id::kRsn: {
            bss_desc->rsn.resize(sizeof(ElementHeader));
            auto header = reinterpret_cast<ElementHeader*>(bss_desc->rsn->data());
            header->id = static_cast<uint8_t>(element_id::kRsn);
            header->len = raw_body.size();
            bss_desc->rsn->insert(bss_desc->rsn->end(), raw_body.begin(), raw_body.end());
            break;
        }
        case element_id::kHtCapabilities:
            if (auto ht_cap = common::ParseHtCapabilities(raw_body)) {
                bss_desc->ht_cap = std::make_unique<wlan_mlme::HtCapabilities>(ht_cap->ToFidl());
            }
            break;
        case element_id::kHtOperation:
            if (auto ht_op = common::ParseHtOperation(raw_body)) {
                bss_desc->ht_op = std::make_unique<wlan_mlme::HtOperation>(ht_op->ToFidl());
            }
            break;
        case element_id::kVhtCapabilities:
            if (auto vht_cap = common::ParseVhtCapabilities(raw_body)) {
                bss_desc->vht_cap = std::make_unique<wlan_mlme::VhtCapabilities>(vht_cap->ToFidl());
            }
            break;
        case element_id::kVhtOperation:
            if (auto vht_op = common::ParseVhtOperation(raw_body)) {
                bss_desc->vht_op = std::make_unique<wlan_mlme::VhtOperation>(vht_op->ToFidl());
            }
            break;
        default:
            break;
        }
    }
}

static void ClassifyRates(Span<const SupportedRate> rates, ::std::vector<uint8_t>* basic,
                          ::std::vector<uint8_t>* op) {
    for (SupportedRate r : rates) {
        if (r.is_basic()) { basic->push_back(r.rate()); }
        op->push_back(r.rate());
    }
}

void FillRates(Span<const SupportedRate> supp_rates, Span<const SupportedRate> ext_supp_rates,
               ::std::vector<uint8_t>* basic, ::std::vector<uint8_t>* op) {
    basic->resize(0);
    op->resize(0);
    ClassifyRates(supp_rates, basic, op);
    ClassifyRates(ext_supp_rates, basic, op);
}

void ParseBeaconElements(Span<const uint8_t> ies, uint8_t rx_channel,
                         wlan_mlme::BSSDescription* bss_desc) {
    std::optional<uint8_t> dsss_chan{};
    Span<const SupportedRate> supp_rates;
    Span<const SupportedRate> ext_supp_rates;

    DoParseBeaconElements(ies, rx_channel, bss_desc, &dsss_chan, &supp_rates, &ext_supp_rates);
    FillRates(supp_rates, ext_supp_rates, &bss_desc->basic_rate_set, &bss_desc->op_rate_set);

    std::optional<CBW> vht_cbw{};
    if (bss_desc->vht_op) { vht_cbw = GetVhtCbw(*bss_desc->vht_op); }
    auto chan = DeriveChannel(rx_channel, dsss_chan, bss_desc->ht_op.get(), vht_cbw);
    bss_desc->chan = common::ToFidl(chan);
}

}  // namespace wlan
