// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "minstrel.h"

#include <wlan/common/channel.h>
#include <wlan/mlme/debug.h>
#include <wlan/protocol/mac.h>

namespace wlan {
namespace wlan_minstrel = ::fuchsia::wlan::minstrel;

zx::duration HeaderTxTimeErp() {
    // TODO(eyw): Implement Erp preamble and header
    return zx::nsec(0);
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

void AddSupportedErp(std::unordered_map<tx_vec_idx_t, TxStats>* tx_stats_map,
                     const std::vector<SupportedRate>& rates) {
    size_t tx_stats_added = 0;
    for (const auto& rate : rates) {
        TxVector tx_vector;
        zx_status_t status = TxVector::FromSupportedRate(rate, &tx_vector);
        ZX_DEBUG_ASSERT(status == ZX_OK);
        // Fuchsia only uses 802.11a/g/n and later data rates for transmission.
        if (tx_vector.phy != WLAN_PHY_ERP) { continue; }
        tx_vec_idx_t tx_vector_idx;
        status = tx_vector.ToIdx(&tx_vector_idx);
        zx::duration perfect_tx_time = TxTimeErp(rate);
        ZX_DEBUG_ASSERT(perfect_tx_time.to_nsecs() != 0);
        debugmstl("%s, tx_time %lu nsec\n", debug::Describe(tx_vector).c_str(),
                  perfect_tx_time.to_nsecs());
        TxStats tx_stats{
            .tx_vector_idx = tx_vector_idx,
            .perfect_tx_time = perfect_tx_time,
        };
        tx_stats_map->emplace(tx_vector_idx, tx_stats);
        ++tx_stats_added;
    }
    debugmstl("%zu ERP added.\n", tx_stats_added);
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
zx::duration PayloadTxTimeHt(CBW cbw, GI gi, size_t mcs_idx) {
    // D_{bps} as defined in IEEE 802.11-2016 Table 19-26
    // Unit: Number of data bits per OFDM symbol (20 MHz channel width)
    constexpr uint16_t bits_per_symbol_list[] = {
        26, 52, 78, 104, 156, 208, 234, 260, /* since VHT */ 312, 347};
    constexpr uint16_t kDataSubCarriers20 = 52;
    constexpr uint16_t kDataSubCarriers40 = 108;
    // TODO(eyw): VHT would have kDataSubCarriers80 = 234 and kDataSubCarriers160 = 468

    ZX_DEBUG_ASSERT(gi == WLAN_GI_400NS || gi == WLAN_GI_800NS);

    int nss = 1 + mcs_idx / kHtNumUniqueMcs;
    int relative_mcs_idx = mcs_idx % kHtNumUniqueMcs;

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

zx::duration TxTimeHt(CBW cbw, GI gi, uint8_t relative_mcs_idx) {
    return HeaderTxTimeHt() + PayloadTxTimeHt(cbw, gi, relative_mcs_idx);
}

// SupportedMcsRx is 78 bit long in IEEE802.11-2016, Figure 9-334
// In reality, devices implement MCS 0-31, sometimes 32, almost never beyond 32.
void AddSupportedHt(std::unordered_map<tx_vec_idx_t, TxStats>* tx_stats_map, CBW cbw, GI gi,
                    const SupportedMcsRxMcsHead& mcs_set) {
    size_t tx_stats_added = 0;
    for (uint8_t mcs_idx = 0; mcs_idx < kHtNumMcs; ++mcs_idx) {
        // Skip if this mcs is not supported
        if (!mcs_set.Support(mcs_idx)) { continue; }

        TxVector tx_vector{
            .phy = WLAN_PHY_HT,
            .gi = gi,
            .cbw = cbw,
            .mcs_idx = mcs_idx,
        };
        tx_vec_idx_t tx_vector_idx;
        zx_status_t status = tx_vector.ToIdx(&tx_vector_idx);
        ZX_DEBUG_ASSERT(status == ZX_OK);
        zx::duration perfect_tx_time = TxTimeHt(cbw, gi, mcs_idx);
        ZX_DEBUG_ASSERT(perfect_tx_time.to_nsecs() != 0);
        debugmstl("%s, tx_time %lu nsec\n", debug::Describe(tx_vector).c_str(),
                  perfect_tx_time.to_nsecs());

        TxStats tx_stats{
            .tx_vector_idx = tx_vector_idx,
            .perfect_tx_time = perfect_tx_time,
        };
        tx_stats_map->emplace(tx_vector_idx, tx_stats);
        ++tx_stats_added;
    }
    debugmstl("%zu HT added with cbw=%s, gi=%s\n", tx_stats_added, ::wlan::common::kCbwStr[cbw],
              debug::Describe(gi).c_str());
}

MinstrelRateSelector::MinstrelRateSelector(TimerManager&& timer_mgr, ProbeSequence&& probe_sequence)
    : timer_mgr_(fbl::move(timer_mgr)), probe_sequence_(std::move(probe_sequence)) {
    // Temporarily suppress compiler complaint about unused variable
    (void)probe_sequence_;
}

void AddErp(std::unordered_map<tx_vec_idx_t, TxStats>* tx_stats_map,
            const wlan_assoc_ctx_t& assoc_ctx) {
    std::vector<SupportedRate> legacy_rates(assoc_ctx.supported_rates_cnt +
                                            assoc_ctx.ext_supported_rates_cnt);

    std::transform(assoc_ctx.supported_rates,
                   assoc_ctx.supported_rates + assoc_ctx.supported_rates_cnt, legacy_rates.begin(),
                   SupportedRate::basic);
    std::transform(assoc_ctx.ext_supported_rates,
                   assoc_ctx.ext_supported_rates + assoc_ctx.ext_supported_rates_cnt,
                   legacy_rates.begin() + assoc_ctx.supported_rates_cnt, SupportedRate::basic);

    debugmstl("Supported rates: %s\n", debug::Describe(legacy_rates).c_str());
    AddSupportedErp(tx_stats_map, legacy_rates);
}

void AddHt(std::unordered_map<tx_vec_idx_t, TxStats>* tx_stats_map, const HtCapabilities& ht_cap) {
    tx_vec_idx_t max_size = kHtNumMcs;
    // TODO(NET-1726): Enable CBW40 support once its information is available from AssocCtx
    const CBW assoc_chan_width = CBW20;
    const bool sgi_20 = ht_cap.ht_cap_info.short_gi_20() == 1;
    const bool sgi_40 = ht_cap.ht_cap_info.short_gi_40() == 1;

    if (sgi_20) { max_size += kHtNumMcs; }
    if (assoc_chan_width == CBW40) {
        max_size += kHtNumMcs;
        if (sgi_40) { max_size += kHtNumMcs; }
    }

    max_size += kErpNumTxVector;  // Taking in to account erp_rates.

    debugmstl("max_size is %d.\n", max_size);

    tx_stats_map->reserve(max_size);

    AddSupportedHt(tx_stats_map, CBW20, WLAN_GI_800NS, ht_cap.mcs_set.rx_mcs_head);
    if (sgi_20) { AddSupportedHt(tx_stats_map, CBW20, WLAN_GI_400NS, ht_cap.mcs_set.rx_mcs_head); }
    if (assoc_chan_width == CBW40) {
        AddSupportedHt(tx_stats_map, CBW40, WLAN_GI_800NS, ht_cap.mcs_set.rx_mcs_head);
        if (sgi_40) {
            AddSupportedHt(tx_stats_map, CBW40, WLAN_GI_400NS, ht_cap.mcs_set.rx_mcs_head);
        }
    }
    debugmstl("tx_stats_map size: %zu.\n", tx_stats_map->size());
}

void MinstrelRateSelector::AddPeer(const wlan_assoc_ctx_t& assoc_ctx) {
    auto addr = common::MacAddr(assoc_ctx.bssid);
    Peer peer{};
    peer.addr = addr;

    HtCapabilities ht_cap;
    constexpr uint32_t kMcsMask0_31 = 0xFFFFFFFF;
    if (assoc_ctx.has_ht_cap) {
        ht_cap = HtCapabilities::FromDdk(assoc_ctx.ht_cap);

        // TODO(eyw): SGI support suppressed. Remove these once they are supported.
        ht_cap.ht_cap_info.set_short_gi_20(false);
        ht_cap.ht_cap_info.set_short_gi_40(false);

        if ((ht_cap.mcs_set.rx_mcs_head.bitmask() & kMcsMask0_31) == 0) {
            errorf("Invalid AssocCtx: HT supported but no valid MCS. %s\n",
                   debug::Describe(ht_cap.mcs_set).c_str());
            ZX_DEBUG_ASSERT(false);
        } else {
            peer.is_ht = true;
            AddHt(&peer.tx_stats_map, ht_cap);
        }
    }

    if (assoc_ctx.supported_rates_cnt + assoc_ctx.ext_supported_rates_cnt > 0) {
        AddErp(&peer.tx_stats_map, assoc_ctx);
    }
    debugmstl("tx_stats_map populated. size: %zu.\n", peer.tx_stats_map.size());

    if (peer.tx_stats_map.size() == 0) {
        errorf("No usable rates for peer %s.\n", addr.ToString().c_str());
        ZX_DEBUG_ASSERT(false);
    }

    debugmstl("Minstrel peer added: %s\n", addr.ToString().c_str());
    if (peer_map_.empty()) {
        ZX_DEBUG_ASSERT(!next_update_event_.IsActive());
        timer_mgr_.Schedule(timer_mgr_.Now() + kMinstrelUpdateInterval, &next_update_event_);
    } else if (GetPeer(addr) != nullptr) {
        warnf("Peer %s already exists. Forgot to clean up?\n", addr.ToString().c_str());
    }
    peer_map_.emplace(addr, std::move(peer));
    outdated_peers_.emplace(addr);
    UpdateStats();
    // TODO(eyw): RemovePeer() for roles other than client.
}

void MinstrelRateSelector::RemovePeer(const common::MacAddr& addr) {
    auto iter = peer_map_.find(addr);
    if (iter == peer_map_.end()) {
        debugmstl("peer %s not found.\n", addr.ToString().c_str());
        return;
    }

    outdated_peers_.erase(addr);
    peer_map_.erase(iter);
    if (peer_map_.empty()) { next_update_event_.Cancel(); }
    debugmstl("peer %s removed.\n", addr.ToString().c_str());
}

void MinstrelRateSelector::HandleTxStatusReport(const wlan_tx_status_t& tx_status) {
    auto peer_addr = common::MacAddr(tx_status.peer_addr);
    auto peer = GetPeer(peer_addr);
    if (peer == nullptr) {
        errorf("Peer [%s] received tx status report after it is removed.\n",
               peer_addr.ToString().c_str());
        return;
    }

    auto tx_stats_map = &peer->tx_stats_map;
    tx_vec_idx_t last_idx = kInvalidTxVectorIdx;
    for (auto entry : tx_status.tx_status_entry) {
        if (entry.tx_vector_idx == kInvalidTxVectorIdx) { break; }
        last_idx = entry.tx_vector_idx;
        (*tx_stats_map)[last_idx].attempts_cur += entry.attempts;
    }
    if (tx_status.success && last_idx != kInvalidTxVectorIdx) {
        (*tx_stats_map)[last_idx].success_cur++;
    }

    outdated_peers_.emplace(peer_addr);
}

bool BetterThroughput(const TxStats& lhs, const TxStats& rhs) {
    return lhs.cur_tp > rhs.cur_tp ||
           (lhs.cur_tp == rhs.cur_tp && lhs.probability > rhs.probability);
}

bool BetterProbability(const TxStats& lhs, const TxStats& rhs) {
    if (lhs.probability >= kMinstrelProbabilityThreshold &&
        rhs.probability >= kMinstrelProbabilityThreshold) {
        // When probability is "high enough", consider throughput instead.
        return lhs.cur_tp > rhs.cur_tp;
    }
    return lhs.probability > rhs.probability;
}

void UpdateStatsPeer(Peer* peer) {
    // Default to the lowest rate supported.
    peer->max_tp = peer->tx_stats_map.cbegin()->first;
    peer->max_probability = peer->max_tp;
    auto* sm = &peer->tx_stats_map;
    for (auto& tx_stats : peer->tx_stats_map) {
        tx_vec_idx_t tx_idx = tx_stats.first;
        auto tsp = &tx_stats.second;
        if (tsp->attempts_cur != 0) {
            float prob = 1.0 * tsp->success_cur / tsp->attempts_cur;
            if (tsp->attempts_total == 0) {
                tsp->probability = prob;
            } else {
                tsp->probability =
                    tsp->probability * kMinstrelExpWeight + prob * (1 - kMinstrelExpWeight);
            }

            if (tsp->attempts_total + tsp->attempts_cur < tsp->attempts_total) {  // overflow
                tsp->attempts_total = 0;
                tsp->success_total = 0;
            } else {
                tsp->attempts_total += tsp->attempts_cur;
                tsp->success_total += tsp->success_cur;
            }
            tsp->attempts_cur = 0;
            tsp->success_cur = 0;
            debugmstl("%s\n", debug::Describe(*tsp).c_str());
        }
        constexpr float kNanoSecondsPerSecond = 1e9;
        // perfect_tx_time is always non-zero as guaranteed by AddSupportedHt and AddSupportedErp
        tsp->cur_tp = kNanoSecondsPerSecond / tsp->perfect_tx_time.to_nsecs() * tsp->probability;

        if (BetterThroughput(*tsp, (*sm)[peer->max_tp])) { peer->max_tp = tx_idx; }

        if (BetterProbability(*tsp, (*sm)[peer->max_probability])) {
            peer->max_probability = tx_idx;
        }
    }
    debugmstl("max_tp: %hu, max_prob: %hu\n", peer->max_tp, peer->max_probability);
}

bool MinstrelRateSelector::HandleTimeout() {
    zx::time now = timer_mgr_.HandleTimeout();
    if (next_update_event_.Triggered(now)) {
        timer_mgr_.Schedule(now + kMinstrelUpdateInterval, &next_update_event_);
        UpdateStats();
        return true;
    } else {
        return false;
    }
}

void MinstrelRateSelector::UpdateStats() {
    for (auto peer_addr : outdated_peers_) {
        auto* peer = GetPeer(peer_addr);
        if (peer != nullptr) {
            debugmstl("%s has update.\n", peer_addr.ToString().c_str());
            UpdateStatsPeer(peer);
        } else {
            ZX_DEBUG_ASSERT(0);
        }
    }
    outdated_peers_.clear();
}

tx_vec_idx_t MinstrelRateSelector::GetNextProbe(Peer* peer) {
    ZX_DEBUG_ASSERT(peer != nullptr);
    tx_vec_idx_t idx;
    do {
        probe_sequence_.Next(&peer->probe_entry, &idx);
    } while (peer->tx_stats_map.count(idx) == 0);  // peer does not support this idx, keep looking
    return idx;
}

Peer* MinstrelRateSelector::GetPeer(const common::MacAddr& addr) {
    auto iter = peer_map_.find(addr);
    if (iter != peer_map_.end()) { return &(iter->second); }
    return nullptr;
}

const Peer* MinstrelRateSelector::GetPeer(const common::MacAddr& addr) const {
    auto iter = peer_map_.find(addr);
    if (iter != peer_map_.end()) { return &(iter->second); }
    return nullptr;
}

zx_status_t MinstrelRateSelector::GetListToFidl(wlan_minstrel::Peers* peers_fidl) const {
    peers_fidl->peers.resize(peer_map_.size());
    size_t idx = 0;
    for (const auto& iter : peer_map_) {
        iter.first.CopyTo((*peers_fidl->peers)[idx++].mutable_data());
    }
    return ZX_OK;
}

wlan_minstrel::StatsEntry TxStats::ToFidl() const {
    return wlan_minstrel::StatsEntry{
        .tx_vector_idx = tx_vector_idx,
        .tx_vec_desc = debug::Describe(tx_vector_idx),
        .success_cur = success_cur,
        .attempts_cur = attempts_cur,
        .probability = probability,
        .cur_tp = cur_tp,
        .success_total = success_total,
        .attempts_total = attempts_total,
    };
}

zx_status_t MinstrelRateSelector::GetStatsToFidl(const common::MacAddr& peer_addr,
                                                 wlan_minstrel::Peer* peer_fidl) const {
    const auto* peer = GetPeer(peer_addr);
    if (peer == nullptr) { return ZX_ERR_NOT_FOUND; }

    peer_addr.CopyTo(peer_fidl->mac_addr.mutable_data());

    peer_fidl->entries.resize(peer->tx_stats_map.size());

    size_t idx = 0;
    for (const auto& [_, tx_stats] : peer->tx_stats_map) {
        (*peer_fidl->entries)[idx++] = tx_stats.ToFidl();
    }
    peer_fidl->max_tp = peer->max_tp;
    peer_fidl->max_probability = peer->max_probability;

    return ZX_OK;
}

bool MinstrelRateSelector::IsActive() const {
    return next_update_event_.IsActive();
}

namespace debug {
// This macro requires char buf[] and size_t offset variable defintions
// in each function.
#define BUFFER(args...)                                                   \
    do {                                                                  \
        offset += snprintf(buf + offset, sizeof(buf) - offset, " " args); \
        if (offset >= sizeof(buf)) {                                      \
            snprintf(buf + sizeof(buf) - 12, 12, " ..(trunc)");           \
            offset = sizeof(buf);                                         \
        }                                                                 \
    } while (false)

std::string Describe(const TxStats& tx_stats) {
    char buf[128];
    size_t offset = 0;

    BUFFER("%s", Describe(tx_stats.tx_vector_idx).c_str());
    BUFFER("succ_c: %zu", tx_stats.success_cur);
    BUFFER("att_c: %zu", tx_stats.attempts_cur);
    BUFFER("succ_t: %zu", tx_stats.success_total);
    BUFFER("att_t: %zu", tx_stats.attempts_total);
    BUFFER("prob: %f", tx_stats.probability);
    BUFFER("tp: %f", tx_stats.cur_tp);

    return std::string(buf, buf + offset);
}
#undef BUFFER
}  // namespace debug

}  // namespace wlan
