// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <wlan/common/channel.h>
#include <wlan/common/element_splitter.h>
#include <wlan/common/parse_element.h>
#include <wlan/mlme/assoc_context.h>

#include <zircon/status.h>

#include <set>

namespace wlan {

PHY AssocContext::DerivePhy() const {
    if (ht_cap.has_value() && ht_op.has_value()) {
        if (vht_cap.has_value() && vht_op.has_value()) {
            return WLAN_PHY_VHT;
        } else {
            return WLAN_PHY_HT;
        }
    }
    return WLAN_PHY_ERP;
}

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

std::optional<std::vector<SupportedRate>> BuildAssocReqSuppRates(
    const std::vector<uint8_t>& ap_basic_rate_set,
    const std::vector<uint8_t>& ap_op_rate_set,
    const std::vector<SupportedRate>& client_rates) {
    std::set<uint8_t> basic(ap_basic_rate_set.cbegin(), ap_basic_rate_set.cend());
    std::set<uint8_t> op(ap_op_rate_set.cbegin(), ap_op_rate_set.cend());

    std::vector<SupportedRate> ap_rates(op.size());
    std::transform(op.cbegin(), op.cend(), ap_rates.begin(), [&basic](uint8_t r) {
        const bool is_basic = std::binary_search(basic.cbegin(), basic.cend(), r);
        return SupportedRate(r, is_basic);
    });

    auto rates = IntersectRatesAp(ap_rates, client_rates);

    size_t num_basic_rates =
        std::count_if(rates.cbegin(), rates.cend(), [](auto& r) { return r.is_basic(); });

    if (num_basic_rates != basic.size()) {
        errorf("Ap demands %zu basic rates. Client supports %zu.\n", basic.size(), num_basic_rates);
        return {};
    }

    return std::move(rates);
}

// TODO(NET-1287): Refactor together with Bss::ParseIE()
std::optional<AssocContext> ParseAssocRespIe(Span<const uint8_t> ie_chains) {
    AssocContext ctx{};
    for (auto [id, raw_body] : common::ElementSplitter(ie_chains)) {
        switch (id) {
        case element_id::kSuppRates: {
            auto rates = common::ParseSupportedRates(raw_body);
            if (!rates) { return {}; }
            ctx.rates.insert(ctx.rates.end(), rates->begin(), rates->end());
            break;
        }
        case element_id::kExtSuppRates: {
            auto rates = common::ParseExtendedSupportedRates(raw_body);
            if (!rates) { return {}; }
            ctx.rates.insert(ctx.rates.end(), rates->begin(), rates->end());
            break;
        }
        case element_id::kHtCapabilities: {
            auto ht_cap = common::ParseHtCapabilities(raw_body);
            if (!ht_cap) { return {}; }
            ctx.ht_cap = {*ht_cap};
            break;
        }
        case element_id::kHtOperation: {
            auto ht_op = common::ParseHtOperation(raw_body);
            if (!ht_op) { return {}; }
            ctx.ht_op = {*ht_op};
            break;
        }
        case element_id::kVhtCapabilities: {
            auto vht_cap = common::ParseVhtCapabilities(raw_body);
            if (!vht_cap) { return {}; }
            ctx.vht_cap = {*vht_cap};
            break;
        }
        case element_id::kVhtOperation: {
            auto vht_op = common::ParseVhtOperation(raw_body);
            if (!vht_op) { return {}; }
            ctx.vht_op = {*vht_op};
            break;
        }
        default:
            break;
        }
    }
    return ctx;
}

AssocContext MakeClientAssocCtx(const wlan_info_t& ifc_info, const wlan_channel_t join_chan) {
    AssocContext assoc_ctx{};
    assoc_ctx.cap = CapabilityInfo::FromDdk(ifc_info.caps);

    auto band_info = FindBand(ifc_info, common::Is5Ghz(join_chan));

    for (uint8_t rate : band_info->basic_rates) {
        if (rate == 0) { break; }  // basic_rates has fixed-length and is "null-terminated".
        // SupportedRates Element can hold only 8 rates.
        assoc_ctx.rates.emplace_back(rate);
    }

    if (ifc_info.supported_phys & WLAN_PHY_HT) {
        assoc_ctx.ht_cap = HtCapabilities::FromDdk(band_info->ht_caps);
    }

    if (band_info->vht_supported) {
        assoc_ctx.vht_cap = VhtCapabilities::FromDdk(band_info->vht_caps);
    }

    return assoc_ctx;
}

std::optional<AssocContext> MakeBssAssocCtx(const AssociationResponse& assoc_resp,
                                            Span<const uint8_t> ie_chains,
                                            const common::MacAddr& peer) {
    auto ctx = ParseAssocRespIe(ie_chains);
    if (!ctx.has_value()) { return {}; }

    ctx->bssid = peer;
    ctx->aid = assoc_resp.aid;
    ctx->cap = assoc_resp.cap;
    return ctx;
}

AssocContext IntersectAssocCtx(const AssocContext& bss, const AssocContext& client) {
    auto result = AssocContext{};

    result.cap = IntersectCapInfo(bss.cap, client.cap);
    result.rates = IntersectRatesAp(bss.rates, client.rates);

    if (bss.ht_cap.has_value() && client.ht_cap.has_value()) {
        // TODO(porce): Supported MCS Set field from the outcome of the intersection
        // requires the conditional treatment depending on the value of the following fields:
        // - "Tx MCS Set Defined"
        // - "Tx Rx MCS Set Not Equal"
        // - "Tx Maximum Number Spatial Streams Supported"
        // - "Tx Unequal Modulation Supported"
        result.ht_cap = IntersectHtCap(bss.ht_cap.value(), client.ht_cap.value());

        // Override the outcome of IntersectHtCap(), which is role agnostic.

        // If AP can't rx STBC, then the client shall not tx STBC.
        // Otherwise, the client shall do what it can do.
        if (bss.ht_cap->ht_cap_info.rx_stbc() == 0) {
            result.ht_cap->ht_cap_info.set_tx_stbc(0);
        } else {
            result.ht_cap->ht_cap_info.set_tx_stbc(client.ht_cap->ht_cap_info.tx_stbc());
        }

        // If AP can't tx STBC, then the client shall not expect to rx STBC.
        // Otherwise, the client shall do what it can do.
        if (bss.ht_cap->ht_cap_info.tx_stbc() == 0) {
            result.ht_cap->ht_cap_info.set_rx_stbc(0);
        } else {
            result.ht_cap->ht_cap_info.set_rx_stbc(client.ht_cap->ht_cap_info.rx_stbc());
        }

        result.ht_op = bss.ht_op;
    }

    if (bss.vht_cap.has_value() && client.vht_cap.has_value()) {
        result.vht_cap = IntersectVhtCap(bss.vht_cap.value(), client.vht_cap.value());
        result.vht_op = bss.vht_op;
    }

    result.is_cbw40_rx =
        result.ht_cap &&
        bss.ht_cap->ht_cap_info.chan_width_set() == HtCapabilityInfo::TWENTY_FORTY &&
        client.ht_cap->ht_cap_info.chan_width_set() == HtCapabilityInfo::TWENTY_FORTY;

    // TODO(porce): Test capabilities and configurations of the client and its BSS.
    // TODO(porce): Ralink dependency on BlockAck, AMPDU handling
    result.is_cbw40_tx = false;

    return result;
}

wlan_assoc_ctx_t AssocContext::ToDdk() const {
    ZX_DEBUG_ASSERT(rates.size() <= WLAN_MAC_MAX_RATES);

    wlan_assoc_ctx_t ddk{};
    bssid.CopyTo(ddk.bssid);
    ddk.aid = aid;

    ddk.listen_interval = listen_interval;
    ddk.phy = phy;
    ddk.chan = chan;

    ddk.rates_cnt = static_cast<uint8_t>(rates.size());
    std::copy(rates.cbegin(), rates.cend(), ddk.rates);

    ddk.has_ht_cap = ht_cap.has_value();
    if (ht_cap.has_value()) { ddk.ht_cap = ht_cap->ToDdk(); }

    ddk.has_ht_op = ht_op.has_value();
    if (ht_op.has_value()) { ddk.ht_op = ht_op->ToDdk(); }

    ddk.has_vht_cap = vht_cap.has_value();
    if (vht_cap.has_value()) { ddk.vht_cap = vht_cap->ToDdk(); }

    ddk.has_vht_op = vht_op.has_value();
    if (vht_op.has_value()) { ddk.vht_op = vht_op->ToDdk(); }

    return ddk;
}

}  // namespace wlan
