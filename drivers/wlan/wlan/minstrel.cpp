// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "minstrel.h"

#include <wlan/common/channel.h>
#include <wlan/mlme/debug.h>
#include <wlan/protocol/mac.h>

namespace wlan {
namespace wlan_minstrel = ::fuchsia::wlan::minstrel;

// If the data rate is too low, do not probe more than twice per update interval
static constexpr uint8_t kMaxSlowProbe = 2;

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

void EmplaceErp(std::unordered_map<tx_vec_idx_t, TxStats>* map, tx_vec_idx_t idx,
                SupportedRate rate) {
    zx::duration time = TxTimeErp(rate);
    ZX_DEBUG_ASSERT(time.to_nsecs() != 0);
    debugmstl("%s, tx_time %lu nsec\n", debug::Describe(idx).c_str(), time.to_nsecs());
    TxStats tx_stats{
        .tx_vector_idx = idx,
        .perfect_tx_time = time,
    };
    map->emplace(idx, tx_stats);
}

std::unordered_set<tx_vec_idx_t> AddSupportedErp(
    std::unordered_map<tx_vec_idx_t, TxStats>* tx_stats_map,
    const std::vector<SupportedRate>& rates) {
    size_t tx_stats_added = 0;
    std::unordered_set<tx_vec_idx_t> basic_rates;
    for (const auto& rate : rates) {
        TxVector tx_vector;
        zx_status_t status = TxVector::FromSupportedRate(rate, &tx_vector);
        ZX_DEBUG_ASSERT(status == ZX_OK);
        // Fuchsia only uses 802.11a/g/n and later data rates for transmission.
        if (tx_vector.phy != WLAN_PHY_ERP) { continue; }
        tx_vec_idx_t tx_vector_idx;
        status = tx_vector.ToIdx(&tx_vector_idx);
        ZX_DEBUG_ASSERT(status == ZX_OK);
        EmplaceErp(tx_stats_map, tx_vector_idx, rate);
        ++tx_stats_added;
        if (rate.is_basic()) {
            debugmstl("basic_rate: %s\n", debug::Describe(tx_vector_idx).c_str());
            basic_rates.emplace(tx_vector_idx);
        }
    }
    debugmstl("%zu ERP added.\n", tx_stats_added);
    if (basic_rates.empty()) { basic_rates.emplace(kErpStartIdx); }
    return basic_rates;
}

bool AddMissingErp(std::unordered_map<tx_vec_idx_t, TxStats>* map, tx_vec_idx_t idx) {
    auto erp_rate = TxVectorIdxToErpRate(idx);
    if (!erp_rate.has_value()) {
        ZX_DEBUG_ASSERT(0);
        return false;
    } else {
        EmplaceErp(map, idx, *erp_rate);
        return true;
    }
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

std::unordered_set<tx_vec_idx_t> AddErp(std::unordered_map<tx_vec_idx_t, TxStats>* tx_stats_map,
                                        const wlan_assoc_ctx_t& assoc_ctx) {
    std::vector<SupportedRate> rates(assoc_ctx.rates_cnt);

    std::transform(assoc_ctx.rates, assoc_ctx.rates + assoc_ctx.rates_cnt, rates.begin(),
                   SupportedRate::raw);

    debugmstl("Supported rates: %s\n", debug::Describe(rates).c_str());
    return AddSupportedErp(tx_stats_map, rates);
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

    if (assoc_ctx.rates_cnt > 0) {
        peer.basic_rates = AddErp(&peer.tx_stats_map, assoc_ctx);
        if (peer.basic_rates.size() > 0) {
            peer.basic_highest =
                *std::max_element(peer.basic_rates.cbegin(), peer.basic_rates.cend());
        }
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
        if (tx_stats_map->count(last_idx) == 0) {
            if (!AddMissingErp(&peer->tx_stats_map, last_idx)) {
                debugmstl("error: Invalid tx_vec_idx: %u.\n", last_idx);
                last_idx = kInvalidTxVectorIdx;
            }
        }
        if (last_idx != kInvalidTxVectorIdx) {
            (*tx_stats_map)[last_idx].attempts_cur += entry.attempts;
        }
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
    auto& tsm = peer->tx_stats_map;
    for (auto& [_, stats] : tsm) {
        if (stats.attempts_cur != 0) {
            float prob = 1.0 * stats.success_cur / stats.attempts_cur;
            if (stats.attempts_total == 0) {
                stats.probability = prob;
            } else {
                stats.probability =
                    stats.probability * kMinstrelExpWeight + prob * (1 - kMinstrelExpWeight);
            }

            if (stats.attempts_total + stats.attempts_cur < stats.attempts_total) {  // overflow
                stats.attempts_total = 0;
                stats.success_total = 0;
            } else {
                stats.attempts_total += stats.attempts_cur;
                stats.success_total += stats.success_cur;
            }
            stats.attempts_cur = 0;
            stats.success_cur = 0;
        }
        constexpr float kNanoSecondsPerSecond = 1e9;
        // perfect_tx_time is always non-zero as guaranteed by AddSupportedHt and AddSupportedErp
        stats.cur_tp = kNanoSecondsPerSecond / stats.perfect_tx_time.to_nsecs() * stats.probability;
    }

    const auto& ctsm = tsm;
    const auto& brs = peer->basic_rates;

    // Pick a random tx vector as the starting point because we will go through them all.
    tx_vec_idx_t max_tp = ctsm.cbegin()->first;
    tx_vec_idx_t max_probability = max_tp;
    tx_vec_idx_t basic_max_probability = peer->basic_highest;
    for (const auto& [idx, stats] : peer->tx_stats_map) {
        if (BetterThroughput(stats, ctsm.at(max_tp))) { max_tp = idx; }
        if (BetterProbability(stats, ctsm.at(max_probability))) { max_probability = idx; }
        if (brs.count(idx) != 0 && BetterProbability(stats, ctsm.at(basic_max_probability))) {
            basic_max_probability = idx;
        }
    }

    peer->max_tp = max_tp;
    peer->max_probability = max_probability;
    peer->basic_max_probability = basic_max_probability;
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

tx_vec_idx_t MinstrelRateSelector::GetTxVectorIdx(const FrameControl& fc,
                                                  const common::MacAddr& peer_addr,
                                                  uint32_t flags) {
    const Peer* peer = GetPeer(peer_addr);
    if (peer == nullptr) { return kErpStartIdx + kErpNumTxVector - 1; }
    if (!fc.IsData()) { return peer->basic_max_probability; }
    const bool needs_reliability = (flags & WLAN_TX_INFO_FLAGS_FAVOR_RELIABILITY) != 0;
    tx_vec_idx_t idx = kInvalidTxVectorIdx;
    idx = GetTxVector(peer_addr, needs_reliability);
    if (idx == kInvalidTxVectorIdx) { idx = peer->basic_highest; }
    return idx;
}

void MinstrelRateSelector::UpdateStats() {
    for (auto peer_addr : outdated_peers_) {
        auto* peer = GetPeer(peer_addr);
        if (peer != nullptr) {
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
        if (probe_sequence_.Next(&peer->probe_entry, &idx)) { ++peer->num_probe_cycles_done; }
    } while (peer->tx_stats_map.count(idx) == 0);  // peer does not support this idx, keep looking
    return idx;
}

tx_vec_idx_t MinstrelRateSelector::GetTxVector(const wlan::common::MacAddr& addr,
                                               bool needs_reliability) {
    Peer* peer = GetPeer(addr);
    if (peer == nullptr) {
        errorf("Error getting tx vector: peer %s does not exist.\n", addr.ToString().c_str());
        ZX_DEBUG_ASSERT(0);
        return kInvalidTxVectorIdx;
    }
    if (needs_reliability) { return peer->max_probability; }
    if (peer->num_pkt_until_next_probe > 0) {
        --peer->num_pkt_until_next_probe;
        return peer->max_tp;
    }
    peer->num_pkt_until_next_probe = kProbeInterval - 1;

    tx_vec_idx_t probe_idx;
    bool should_not_probe = true;
    zx::duration baseline_tx_time = peer->tx_stats_map[peer->max_probability].perfect_tx_time;
    while (should_not_probe) {
        probe_idx = GetNextProbe(peer);
        const TxStats& tx_stats = peer->tx_stats_map[probe_idx];
        // tx vector does not need probing if:
        // 1) It is the highest basic rate
        // 2) It has more attempts than others
        // 3) It is slower than max_probability and has been probed at least kMaxSlowProbe times
        should_not_probe = (probe_idx == peer->basic_highest) ||
                           (tx_stats.attempts_cur > peer->num_probe_cycles_done) ||
                           ((tx_stats.perfect_tx_time > baseline_tx_time) &&
                            (tx_stats.attempts_cur >= kMaxSlowProbe));
    }
    ++peer->probes;
    ++peer->tx_stats_map[probe_idx].probes_total;
    return probe_idx;
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
        .probes_total = probes_total,
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
    peer_fidl->basic_highest = peer->basic_highest;
    peer_fidl->basic_max_probability = peer->basic_max_probability;
    peer_fidl->probes = peer->probes;

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
