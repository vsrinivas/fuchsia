// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "minstrel.h"

#include <wlan/mlme/debug.h>

#include <random>

namespace wlan {

zx::duration HeaderTxTimeErp() {
    // TODO(eyw): Implement Erp preamble and header
    return zx::nsec(0);
}

// Use pseudo MCS index for ERP
// 0: BPSK, 1/2 -> Data rate 6 Mbps
// 1: BPSK, 3/4 -> Data rate 9 Mbps
// 2: QPSK, 1/2 -> Data rate 12 Mbps
// 3: QPSK, 3/4 -> Data rate 18 Mbps
// 4: 16-QAM, 1/2 -> Data rate 24 Mbps
// 5: 16-QAM, 3/4 -> Data rate 36 Mbps
// 6: 64-QAM, 2/3 -> Data rate 48 Mbps
// 7: 64-QAM, 3/4 -> Data rate 54 Mbps
constexpr size_t kUnsupportedErpMcsIdx = 8;
size_t GetErpPseudoMcsIdx(SupportedRate rate) {
    switch (rate.rate()) {
    case 12:
        return 0;
    case 18:
        return 1;
    case 24:
        return 2;
    case 36:
        return 3;
    case 48:
        return 4;
    case 72:
        return 5;
    case 96:
        return 6;
    case 108:
        return 7;
    default:
        uint8_t rate_val = rate.rate();
        if (rate_val == 2 || rate_val == 4 || rate_val == 11 || rate_val == 22) {
            debugmstl("CCK rate %u skipped.\n", rate_val);
        } else {
            errorf("Invalid rate %u in legacy_rates.\n", rate_val);
            ZX_DEBUG_ASSERT(false);
        }
        return kUnsupportedErpMcsIdx;
    }
}

zx::duration PayloadTxTimeErp(SupportedRate rate) {
    // D_{bps} as defined in IEEE 802.11-2016 Table 17-4
    // Unit: Number of data bits per OFDM symbol
    uint16_t bits_per_symbol = rate.rate() * 2;
    constexpr int kTxTimePerSymbol = 4000;  // nanoseconds.

    uint32_t total_time = kTxTimePerSymbol * 8 * kMinstrelFrameLength / bits_per_symbol;
    return zx::nsec(total_time);
}

zx::duration TxTimeErp(SupportedRate rate) {
    return HeaderTxTimeErp() + PayloadTxTimeErp(rate);
}

std::vector<TxParamSet> GetSupportedErp(const std::vector<SupportedRate>& rates) {
    std::vector<TxParamSet> tx_params_to_add;
    tx_params_to_add.reserve(kNumUniqueMcsHt);
    for (const auto& rate : rates) {
        size_t pseudo_mcs = GetErpPseudoMcsIdx(rate);
        if (pseudo_mcs == kUnsupportedErpMcsIdx) { continue; }
        TxParamSet tx_params_set{};
        tx_params_set.phy = WLAN_PHY_ERP;
        tx_params_set.mcs_index = pseudo_mcs;
        tx_params_set.perfect_tx_time = TxTimeErp(rate);
        debugmstl("ERP added: mcs: %u, tx_time %lu nsec\n", tx_params_set.mcs_index,
                  tx_params_set.perfect_tx_time.to_nsecs());
        tx_params_to_add.push_back(std::move(tx_params_set));
    }
    return tx_params_to_add;
}

// MCS 0-7->nss=1, 8-15->nss=2, 16-23->nss=3, 24-31->nss=4
// If any of the 8 MCS with a particular nss is supported, max_nss should be incremented
uint8_t HtMcsSetToNss(const SupportedMcsRxMcsHead& mcs_set) {
    uint64_t bitmask = mcs_set.bitmask();
    uint32_t group_mask = 0x11111111;
    uint8_t max_nss = 0;
    while (bitmask & group_mask) {
        ++max_nss;
        group_mask <<= kNumUniqueMcsHt;
    }
    return max_nss;
}

zx::duration HeaderTxTimeHt() {
    // TODO(eyw): Implement Plcp preamble and header
    return zx::nsec(0);
}

// relative_mcs_idx is the index for combination of (modulation, coding rate) tuple
// when listed in the same order as MCS Index, without nss. i.e.
// 0: BPSK, 1/2
// 1: QPSK, 1/2
// 2: QPSK, 3/4
// 3: 16-QAM, 1/2
// 4: 16-QAM, 3/4
// 5: 64-QAM, 2/3
// 6: 64-QAM, 3/4
// 7: 64-QAM, 5/6
// 8: 256-QAM, 3/4 (since VHT)
// 9: 256-QAM, 5/6 (since VHT)
zx::duration PayloadTxTimeHt(uint8_t nss, CBW cbw, GI gi, size_t relative_mcs_idx) {
    // D_{bps} as defined in IEEE 802.11-2016 Table 19-26
    // Unit: Number of data bits per OFDM symbol (20 MHz channel width)
    constexpr uint16_t bits_per_symbol_list[] = {
        26, 52, 78, 104, 156, 208, 234, 260, /* since VHT */ 312, 347};
    constexpr uint16_t kDataSubCarriers20 = 52;
    constexpr uint16_t kDataSubCarriers40 = 108;
    // TODO(eyw): VHT would have kDataSubCarriers80 = 234 and kDataSubCarriers160 = 468

    ZX_DEBUG_ASSERT(gi == WLAN_GI_400NS || gi == WLAN_GI_800NS);

    uint16_t bits_per_symbol = bits_per_symbol_list[relative_mcs_idx];
    if (cbw == CBW40) {
        bits_per_symbol = bits_per_symbol * kDataSubCarriers40 / kDataSubCarriers20;
    }

    constexpr int kTxTimePerSymbolGi800 = 4000;  // nanoseconds.
    constexpr int kTxTimePerSymbolGi400 = 3600;  // nanoseconds.
    // Perform multiplication before division to prevent precision loss
    uint32_t total_time =
        kTxTimePerSymbolGi800 * 8 * kMinstrelFrameLength / (nss * bits_per_symbol);

    if (gi == WLAN_GI_400NS) {
        total_time =
            800 + (kTxTimePerSymbolGi400 * 8 * kMinstrelFrameLength / (nss * bits_per_symbol));
    }
    return zx::nsec(total_time);
}

zx::duration TxTimeHt(uint8_t nss, CBW cbw, GI gi, uint8_t relative_mcs_idx) {
    return HeaderTxTimeHt() + PayloadTxTimeHt(nss, cbw, gi, relative_mcs_idx);
}

// SupportedMcsRx is 78 bit long in IEEE802.11-2016, Figure 9-334
// In reality, devices implement MCS 0-31, sometimes 32, almost never beyond 32.
std::vector<TxParamSet> GetSupportedHt(uint8_t nss, CBW cbw, GI gi,
                                       const SupportedMcsRxMcsHead& mcs_set) {
    std::vector<TxParamSet> tx_params_to_add;
    tx_params_to_add.reserve(kNumUniqueMcsHt);
    for (uint8_t relative_mcs_idx = 0; relative_mcs_idx < kNumUniqueMcsHt; ++relative_mcs_idx) {
        uint8_t mcs_index = (nss - 1) * kNumUniqueMcsHt + relative_mcs_idx;

        // Skip if this mcs is not supported
        if (!mcs_set.Support(mcs_index)) { continue; }

        TxParamSet tx_param_set{};
        tx_param_set.phy = WLAN_PHY_HT;
        tx_param_set.nss = nss;
        tx_param_set.gi = gi;
        tx_param_set.cbw = cbw;
        tx_param_set.mcs_index = mcs_index;
        tx_param_set.perfect_tx_time = TxTimeHt(nss, cbw, gi, relative_mcs_idx);
        debugmstl("HT added: mcs %u, tx_time %lu nsec\n", tx_param_set.mcs_index,
                  tx_param_set.perfect_tx_time.to_nsecs());
        tx_params_to_add.emplace_back(std::move(tx_param_set));
    }
    return tx_params_to_add;
}

MinstrelRateSelector::MinstrelRateSelector(TimerManager&& timer_mgr)
    : timer_mgr_(fbl::move(timer_mgr)) {}

void AddErp(std::vector<TxParamSet>* tx_params_list, const wlan_assoc_ctx_t& assoc_ctx) {
    std::vector<SupportedRate> legacy_rates(assoc_ctx.supported_rates_cnt +
                                            assoc_ctx.ext_supported_rates_cnt);

    std::transform(assoc_ctx.supported_rates,
                   assoc_ctx.supported_rates + assoc_ctx.supported_rates_cnt, legacy_rates.begin(),
                   SupportedRate::basic);
    std::transform(assoc_ctx.ext_supported_rates,
                   assoc_ctx.ext_supported_rates + assoc_ctx.ext_supported_rates_cnt,
                   legacy_rates.begin() + assoc_ctx.supported_rates_cnt, SupportedRate::basic);

    debugmstl("Legacy rates: %s\n", debug::Describe(legacy_rates).c_str());
    *tx_params_list = GetSupportedErp(legacy_rates);
    debugmstl("%zu ERP added.\n", tx_params_list->size());
}

void AddHt(std::vector<TxParamSet>* tx_params_list, const HtCapabilities& ht_cap) {
    int max_size = kNumUniqueMcsHt;
    uint8_t assoc_chan_width = 20;

    if (ht_cap.ht_cap_info.chan_width_set() == HtCapabilityInfo::ChanWidthSet::TWENTY_FORTY) {
        assoc_chan_width = 40;
        max_size *= 2;
    }
    uint8_t assoc_sgi = WLAN_GI_800NS;  // SGI not supported yet
    uint8_t assoc_nss = HtMcsSetToNss(ht_cap.mcs_set.rx_mcs_head);
    max_size = max_size * assoc_nss + kNumUniqueMcsHt;  // Taking in to account legacy_rates.

    debugmstl("max_size is %d.\n", max_size);

    tx_params_list->reserve(max_size);

    // Enumerate all combinations of chan_width, gi, nss and mcs_index
    for (uint8_t bw = 20; bw <= assoc_chan_width; bw *= 2) {
        uint8_t cbw = bw == 20 ? CBW20 : CBW40;
        for (uint8_t gi = 1 << 0; gi <= assoc_sgi; gi <<= 1) {
            for (uint8_t nss = 1; nss <= assoc_nss; nss++) {
                auto tx_params_to_add = GetSupportedHt(
                    nss, static_cast<CBW>(cbw), static_cast<GI>(gi), ht_cap.mcs_set.rx_mcs_head);
                debugmstl("%zu HT added with nss=%u, cbw=%u, gi=%u\n", tx_params_to_add.size(), nss,
                          cbw, gi);
                tx_params_list->insert(tx_params_list->end(), tx_params_to_add.begin(),
                                       tx_params_to_add.end());
            }
        }
    }

    debugmstl("tx_params_list size: %zu.\n", tx_params_list->size());
}

void MinstrelRateSelector::AddPeer(const wlan_assoc_ctx_t& assoc_ctx) {
    auto addr = common::MacAddr(assoc_ctx.bssid);
    Peer peer{};
    peer.addr = addr;

    if (assoc_ctx.supported_rates_cnt + assoc_ctx.ext_supported_rates_cnt > 0) {
        AddErp(&peer.tx_params_list, assoc_ctx);
    }

    HtCapabilities ht_cap;
    constexpr uint32_t kMcsMask0_31 = 0xFFFFFFFF;
    if (assoc_ctx.has_ht_cap) {
        ht_cap = HtCapabilities::FromDdk(assoc_ctx.ht_cap);
        if ((ht_cap.mcs_set.rx_mcs_head.bitmask() & kMcsMask0_31) == 0) {
            errorf("Invalid AssocCtx: HT supported but no valid MCS. %s\n",
                   debug::Describe(ht_cap.mcs_set).c_str());
            ZX_DEBUG_ASSERT(false);
        } else {
            peer.is_ht = true;
            AddHt(&peer.tx_params_list, ht_cap);
        }
    }

    if (peer.tx_params_list.size() == 0) {
        errorf("No usable rates for peer %s.\n", addr.ToString().c_str());
        ZX_DEBUG_ASSERT(false);
    }

    peer.tx_stats_list = std::vector<TxStats>(peer.tx_params_list.size());
    debugmstl("Minstrel peer added: %s\n", addr.ToString().c_str());
    peer_map_.emplace(addr, std::move(peer));
    // TODO(eyw): RemovePeer() needs to be called at de-association.
}

void MinstrelRateSelector::RemovePeer(const common::MacAddr& addr) {
    auto iter = peer_map_.find(addr);
    if (iter == peer_map_.end()) { return; }

    peer_map_.erase(iter);
}

void MinstrelRateSelector::HandleTxStatusReport(const wlan_tx_status_t& tx_status) {
    auto peer_addr = common::MacAddr(tx_status.peer_addr);
    auto peer = GetPeer(peer_addr);
    ZX_DEBUG_ASSERT(peer != nullptr);
    if (peer == nullptr) {
        errorf("Peer [%s] does not exist for tx status report.\n", peer_addr.ToString().c_str());
        return;
    }
    auto& tx_stats = peer->tx_stats_list[tx_status.rate_idx];
    tx_stats.attempts += tx_status.retries + 1;
    tx_stats.success += tx_status.success ? 1 : 0;
    peer->has_update = true;
}

void MinstrelRateSelector::UpdateStats() {
    for (auto iter = peer_map_.begin(); iter != peer_map_.end(); ++iter) {
        if (!iter->second.has_update) { continue; }
        iter->second.has_update = false;

        // TODO(eyw): Loop through all tx_stats and pick the best combination for next update period
    }
}

Peer* MinstrelRateSelector::GetPeer(const common::MacAddr& addr) {
    auto iter = peer_map_.find(addr);
    if (iter != peer_map_.end()) { return &(iter->second); }
    return nullptr;
}
}  // namespace wlan
