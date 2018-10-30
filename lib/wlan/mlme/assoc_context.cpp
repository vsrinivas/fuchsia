// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <wlan/common/channel.h>
#include <wlan/mlme/assoc_context.h>

#include <set>

namespace wlan {

const wlan_band_info_t* FindBand(const wlan_info_t& ifc_info, bool is_5ghz) {
    ZX_DEBUG_ASSERT(ifc_info.num_bands <= WLAN_MAX_BANDS);

    for (uint8_t idx = 0; idx < ifc_info.num_bands; idx++) {
        auto bi = &ifc_info.bands[idx];
        auto base_freq = bi->supported_channels.base_freq;

        if (is_5ghz && base_freq == common::kBaseFreq5Ghz) {
            return bi;
        } else if (!is_5ghz && base_freq == common::kBaseFreq2Ghz) {
            return bi;
        }
    }

    return nullptr;
}

AssocContext ToAssocContext(const wlan_info_t& ifc_info, const wlan_channel_t join_chan) {
    AssocContext assoc_ctx{};
    assoc_ctx.cap = CapabilityInfo::FromDdk(ifc_info.caps);

    auto band_info = FindBand(ifc_info, common::Is5Ghz(join_chan));

    for (uint8_t rate : band_info->basic_rates) {
        if (rate == 0) { break; }  // basic_rates has fixed-length and is "null-terminated".
        // SupportedRates Element can hold only 8 rates.
        assoc_ctx.rates.emplace_back(rate);
    }

    if (ifc_info.supported_phys & WLAN_PHY_HT) {
        assoc_ctx.ht_cap = std::make_optional(HtCapabilities::FromDdk(band_info->ht_caps));
    }

    if (band_info->vht_supported) {
        assoc_ctx.vht_cap = std::make_optional(VhtCapabilities::FromDdk(band_info->vht_caps));
    }

    return assoc_ctx;
}

std::optional<std::vector<SupportedRate>> BuildAssocReqSuppRates(
    const ::fidl::VectorPtr<uint8_t>& ap_basic_rate_set,
    const ::fidl::VectorPtr<uint8_t>& ap_op_rate_set,
    const std::vector<SupportedRate>& client_rates) {
    std::set<uint8_t> basic(ap_basic_rate_set->cbegin(), ap_basic_rate_set->cend());
    std::set<uint8_t> op(ap_op_rate_set->cbegin(), ap_op_rate_set->cend());

    std::vector<SupportedRate> ap_rates(op.size());
    std::transform(op.cbegin(), op.cend(), ap_rates.begin(), [&basic](uint8_t r) {
        const bool is_basic = std::binary_search(basic.cbegin(), basic.cend(), r);
        return SupportedRate(r, is_basic);
    });

    auto rates = IntersectRatesAp(ap_rates, client_rates);

    size_t num_basic_rates =
        std::count_if(rates.cbegin(), rates.cend(), [](auto& r) { return r.is_basic(); });

    if (num_basic_rates != basic.size()) {
        errorf("Ap demands %zu basic rates. Client supports %zu.\n", basic.size(), rates.size());
        return {};
    }

    return std::move(rates);
}

// TODO(NET-1287): Refactor together with Bss::ParseIE()
zx_status_t ParseAssocRespIe(const uint8_t* ie_chains, size_t ie_chains_len,
                             AssocContext* assoc_ctx) {
    ZX_DEBUG_ASSERT(assoc_ctx != nullptr);

    ElementReader reader(ie_chains, ie_chains_len);
    while (reader.is_valid()) {
        const ElementHeader* hdr = reader.peek();
        if (hdr == nullptr) { break; }

        switch (hdr->id) {
        case element_id::kSuppRates: {
            auto ie = reader.read<SupportedRatesElement>();
            if (ie == nullptr) { return ZX_ERR_INTERNAL; }
            for (uint8_t i = 0; i < ie->hdr.len; i++) {
                assoc_ctx->rates.push_back(ie->rates[i]);
            }
            break;
        }
        case element_id::kExtSuppRates: {
            auto ie = reader.read<ExtendedSupportedRatesElement>();
            if (ie == nullptr) { return ZX_ERR_INTERNAL; }
            for (uint8_t i = 0; i < ie->hdr.len; i++) {
                assoc_ctx->rates.push_back(ie->rates[i]);
            }
            break;
        }
        case element_id::kHtCapabilities: {
            auto ie = reader.read<HtCapabilitiesElement>();
            if (ie == nullptr) { return ZX_ERR_INTERNAL; }
            assoc_ctx->ht_cap = std::make_optional(ie->body);
            break;
        }
        case element_id::kHtOperation: {
            auto ie = reader.read<HtOperationElement>();
            if (ie == nullptr) { return ZX_ERR_INTERNAL; }
            assoc_ctx->ht_op = std::make_optional(ie->body);
            break;
        }
        case element_id::kVhtCapabilities: {
            auto ie = reader.read<VhtCapabilitiesElement>();
            if (ie == nullptr) { return ZX_ERR_INTERNAL; }
            assoc_ctx->vht_cap = std::make_optional(ie->body);
            break;
        }
        case element_id::kVhtOperation: {
            auto ie = reader.read<VhtOperationElement>();
            if (ie == nullptr) { return ZX_ERR_INTERNAL; }
            assoc_ctx->vht_op = std::make_optional(ie->body);
            break;
        }
        default:
            reader.skip(sizeof(ElementHeader) + hdr->len);
            break;
        }
    }

    return ZX_OK;
}

}  // namespace wlan
