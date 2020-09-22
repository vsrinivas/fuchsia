// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "minstrel.h"

#include <memory>

#include <ddk/hw/wlan/wlaninfo.h>
#include <wlan/common/channel.h>
#include <wlan/mlme/debug.h>
#include <wlan/protocol/mac.h>

namespace wlan {
namespace wlan_minstrel = ::fuchsia::wlan::minstrel;

// If the data rate is too low, do not probe more than twice per update interval
static constexpr uint8_t kMaxSlowProbe = 2;
// If success rate is below (1 - kProbabilityThreshold), only probe once every
// this many cycles.
static constexpr uint8_t kDeadProbeCycleCount = 32;

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

zx::duration TxTimeErp(SupportedRate rate) { return HeaderTxTimeErp() + PayloadTxTimeErp(rate); }

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
    if (tx_vector.phy != WLAN_INFO_PHY_TYPE_ERP) {
      continue;
    }
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
  if (basic_rates.empty()) {
    basic_rates.emplace(kErpStartIdx);
  }
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

// relative_mcs_idx is the index for combination of (modulation, coding rate)
// tuple when listed in the same order as MCS Index, without nss. i.e. 0: BPSK,
// 1/2 1: QPSK, 1/2 2: QPSK, 3/4 3: 16-QAM, 1/2 4: 16-QAM, 3/4 5: 64-QAM, 2/3 6:
// 64-QAM, 3/4 7: 64-QAM, 5/6 8: 256-QAM, 3/4 (since VHT) 9: 256-QAM, 5/6 (since
// VHT)
zx::duration PayloadTxTimeHt(wlan_channel_bandwidth_t cbw, wlan_gi_t gi, size_t mcs_idx) {
  // D_{bps} as defined in IEEE 802.11-2016 Table 19-26
  // Unit: Number of data bits per OFDM symbol (20 MHz channel width)
  constexpr uint16_t bits_per_symbol_list[] = {
      26, 52, 78, 104, 156, 208, 234, 260, /* since VHT */ 312, 347};
  constexpr uint16_t kDataSubCarriers20 = 52;
  constexpr uint16_t kDataSubCarriers40 = 108;
  // TODO(eyw): VHT would have kDataSubCarriers80 = 234 and kDataSubCarriers160
  // = 468

  ZX_DEBUG_ASSERT(gi == WLAN_GI__400NS || gi == WLAN_GI__800NS);

  int nss = 1 + mcs_idx / kHtNumUniqueMcs;
  int relative_mcs_idx = mcs_idx % kHtNumUniqueMcs;

  uint16_t bits_per_symbol = bits_per_symbol_list[relative_mcs_idx];
  if (cbw == WLAN_CHANNEL_BANDWIDTH__40) {
    bits_per_symbol = bits_per_symbol * kDataSubCarriers40 / kDataSubCarriers20;
  }

  constexpr int kTxTimePerSymbolGi800 = 4000;  // nanoseconds.
  constexpr int kTxTimePerSymbolGi400 = 3600;  // nanoseconds.
  // Perform multiplication before division to prevent precision loss
  uint32_t total_time = kTxTimePerSymbolGi800 * 8 * kMinstrelFrameLength / (nss * bits_per_symbol);

  if (gi == WLAN_GI__400NS) {
    total_time = 800 + (kTxTimePerSymbolGi400 * 8 * kMinstrelFrameLength / (nss * bits_per_symbol));
  }
  return zx::nsec(total_time);
}

zx::duration TxTimeHt(wlan_channel_bandwidth_t cbw, wlan_gi_t gi, uint8_t relative_mcs_idx) {
  return HeaderTxTimeHt() + PayloadTxTimeHt(cbw, gi, relative_mcs_idx);
}

// SupportedMcsRx is 78 bit long in IEEE802.11-2016, Figure 9-334
// In reality, devices implement MCS 0-31, sometimes 32, almost never beyond 32.
void AddSupportedHt(std::unordered_map<tx_vec_idx_t, TxStats>* tx_stats_map,
                    wlan_channel_bandwidth_t cbw, wlan_gi_t gi,
                    const SupportedMcsRxMcsHead& mcs_set) {
  size_t tx_stats_added = 0;
  for (uint8_t mcs_idx = 0; mcs_idx < kHtNumMcs; ++mcs_idx) {
    // Skip if this mcs is not supported
    if (!mcs_set.Support(mcs_idx)) {
      continue;
    }

    TxVector tx_vector{
        .phy = WLAN_INFO_PHY_TYPE_HT,
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
  debugmstl("%zu HT added with cbw=%s, gi=%s\n", tx_stats_added, ::wlan::common::CbwStr(cbw),
            debug::Describe(gi).c_str());
}

MinstrelRateSelector::MinstrelRateSelector(std::unique_ptr<Timer>&& timer,
                                           ProbeSequence&& probe_sequence,
                                           zx::duration update_interval)
    : timer_mgr_(std::move(timer)),
      probe_sequence_(std::move(probe_sequence)),
      update_interval_(update_interval) {}

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
  // TODO(fxbug.dev/28744): Enable CBW40 support once its information is available from
  // AssocCtx
  const uint8_t assoc_chan_width = WLAN_CHANNEL_BANDWIDTH__20;
  const bool sgi_20 = ht_cap.ht_cap_info.short_gi_20() == 1;
  const bool sgi_40 = ht_cap.ht_cap_info.short_gi_40() == 1;

  if (sgi_20) {
    max_size += kHtNumMcs;
  }
  if (assoc_chan_width == WLAN_CHANNEL_BANDWIDTH__40) {
    max_size += kHtNumMcs;
    if (sgi_40) {
      max_size += kHtNumMcs;
    }
  }

  max_size += kErpNumTxVector;  // Taking in to account erp_rates.

  debugmstl("max_size is %d.\n", max_size);

  tx_stats_map->reserve(max_size);

  AddSupportedHt(tx_stats_map, WLAN_CHANNEL_BANDWIDTH__20, WLAN_GI__800NS,
                 ht_cap.mcs_set.rx_mcs_head);
  if (sgi_20) {
    AddSupportedHt(tx_stats_map, WLAN_CHANNEL_BANDWIDTH__20, WLAN_GI__400NS,
                   ht_cap.mcs_set.rx_mcs_head);
  }
  if (assoc_chan_width == WLAN_CHANNEL_BANDWIDTH__40) {
    AddSupportedHt(tx_stats_map, WLAN_CHANNEL_BANDWIDTH__40, WLAN_GI__800NS,
                   ht_cap.mcs_set.rx_mcs_head);
    if (sgi_40) {
      AddSupportedHt(tx_stats_map, WLAN_CHANNEL_BANDWIDTH__40, WLAN_GI__400NS,
                     ht_cap.mcs_set.rx_mcs_head);
    }
  }
  debugmstl("tx_stats_map size: %zu.\n", tx_stats_map->size());
}

void MinstrelRateSelector::AddPeer(const wlan_assoc_ctx_t& assoc_ctx) {
  auto addr = common::MacAddr(assoc_ctx.bssid);
  Peer peer{};
  peer.addr = addr;

  {
    std::lock_guard<std::mutex> guard(*peer.update_lock);
    HtCapabilities ht_cap;
    constexpr uint32_t kMcsMask0_31 = 0xFFFFFFFF;
    if (assoc_ctx.has_ht_cap) {
      ht_cap = HtCapabilities::FromDdk(assoc_ctx.ht_cap);

      // TODO(eyw): SGI support suppressed. Remove these once they are
      // supported.
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
        peer.basic_highest = *std::max_element(peer.basic_rates.cbegin(), peer.basic_rates.cend());
      }
    }
    debugmstl("tx_stats_map populated. size: %zu.\n", peer.tx_stats_map.size());

    if (peer.tx_stats_map.size() == 0) {
      errorf("No usable rates for peer %s.\n", addr.ToString().c_str());
      ZX_DEBUG_ASSERT(false);
    }
  }

  debugmstl("Minstrel peer added: %s\n", addr.ToString().c_str());
  if (peer_map_.empty()) {
    ZX_DEBUG_ASSERT(next_update_ == TimeoutId{});
    timer_mgr_.Schedule(timer_mgr_.Now() + update_interval_, {}, &next_update_);
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
  if (peer_map_.empty()) {
    timer_mgr_.Cancel(next_update_);
    next_update_ = {};
  }
  debugmstl("peer %s removed.\n", addr.ToString().c_str());
}

void MinstrelRateSelector::HandleTxStatusReport(const wlan_tx_status_t& tx_status) {
  auto peer_addr = common::MacAddr(tx_status.peer_addr);
  auto peer = GetPeer(peer_addr);
  if (peer == nullptr) {
    debugmstl("Peer [%s] received tx status report after it is removed.\n",
              peer_addr.ToString().c_str());
    return;
  }

  std::lock_guard<std::mutex> guard(*peer->update_lock);
  auto tx_stats_map = &peer->tx_stats_map;
  tx_vec_idx_t last_idx = kInvalidTxVectorIdx;
  for (auto entry : tx_status.tx_status_entry) {
    if (entry.tx_vector_idx == kInvalidTxVectorIdx) {
      break;
    }
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

inline constexpr bool TxStats::PhyPreferredOver(const wlan::TxStats& other) const {
  // based on experiment, If HT is supported, it is better not to use ERP for
  // data frames. With ralink RT5592 and Netgear Nighthawk X10, approximately 80
  // feet away, HT/ERP tx throughput < 1 Mbps, HT only tx 4-8 Mbps
  // TODO(fxbug.dev/29488): Revisit with VHT support.
  return IsHt(tx_vector_idx) && !IsHt(other.tx_vector_idx);
}

inline constexpr bool TxStats::ThroughputHigherThan(const TxStats& other) const {
  return cur_tp > other.cur_tp || (cur_tp == other.cur_tp && probability > other.probability);
}

inline constexpr bool TxStats::ProbabilityHigherThan(const TxStats& other) const {
  if (probability >= kMinstrelProbabilityThreshold &&
      other.probability >= kMinstrelProbabilityThreshold) {
    // When probability is "high enough", consider throughput instead.
    return cur_tp > other.cur_tp;
  }
  return probability > other.probability;
}

inline constexpr bool IsTxUnlikely(const TxStats& ts) {
  return ts.probability < 1.0f - kMinstrelProbabilityThreshold;
}

void UpdateStatsPeer(Peer* peer) {
  std::lock_guard<std::mutex> guard(*peer->update_lock);
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
        stats.attempts_total = stats.attempts_cur;
        stats.success_total = stats.success_cur;
      } else {
        stats.attempts_total += stats.attempts_cur;
        stats.success_total += stats.success_cur;
      }
      stats.attempts_cur = 0;
      stats.success_cur = 0;
      stats.probe_cycles_skipped = 0;
    } else {
      ++stats.probe_cycles_skipped;
    }
    constexpr float kNanoSecondsPerSecond = 1e9;
    // perfect_tx_time is always non-zero as guaranteed by AddSupportedHt and
    // AddSupportedErp
    stats.cur_tp = kNanoSecondsPerSecond / stats.perfect_tx_time.to_nsecs() * stats.probability;
  }

  const auto& ctsm = tsm;
  const auto& brs = peer->basic_rates;

  // Pick a random tx vector as the starting point because we will go through
  // them all.
  tx_vec_idx_t max_tp = ctsm.cbegin()->first;
  tx_vec_idx_t max_probability = max_tp;
  tx_vec_idx_t basic_max_probability = peer->basic_highest;
  for (const auto& [idx, stats] : peer->tx_stats_map) {
    if ((!IsTxUnlikely(stats) && stats.PhyPreferredOver(ctsm.at(max_tp))) ||
        stats.ThroughputHigherThan(ctsm.at(max_tp))) {
      max_tp = idx;
    }
    if ((!IsTxUnlikely(stats) && stats.PhyPreferredOver(ctsm.at(max_probability))) ||
        stats.ProbabilityHigherThan(ctsm.at(max_probability))) {
      max_probability = idx;
    }
    if (brs.count(idx) != 0 && stats.ProbabilityHigherThan(ctsm.at(basic_max_probability))) {
      basic_max_probability = idx;
    }
  }

  peer->max_tp = max_tp;
  peer->max_probability = max_probability;
  peer->basic_max_probability = basic_max_probability;
}

bool MinstrelRateSelector::HandleTimeout() {
  bool handled = false;
  timer_mgr_.HandleTimeout([&](auto now, auto _event, auto timeout_id) {
    if (next_update_ == timeout_id) {
      timer_mgr_.Schedule(now + update_interval_, {}, &next_update_);
      UpdateStats();
      handled = true;
    }
  });
  return handled;
}

tx_vec_idx_t GetNextProbe(Peer* peer, const ProbeSequence& probe_sequence) {
  std::lock_guard<std::mutex> gaurd(*peer->update_lock);
  tx_vec_idx_t probe_idx = kInvalidTxVectorIdx;
  zx::duration baseline_tx_time = peer->tx_stats_map[peer->max_probability].perfect_tx_time;
  auto potential_probes = peer->tx_stats_map.size();
  if (potential_probes == 1) {
    return peer->max_tp;
  }
  while (potential_probes > 0) {
    --potential_probes;
    do {
      if (probe_sequence.Next(&peer->probe_entry, &probe_idx)) {
        ++peer->num_probe_cycles_done;
      }
    } while (peer->tx_stats_map.count(probe_idx) == 0);  // not supported, keep looking
    const TxStats& tx_stats = peer->tx_stats_map[probe_idx];
    const bool skip =
        // These are used by default so no probing needed
        probe_idx == peer->basic_max_probability || probe_idx == peer->basic_highest ||
        probe_idx == peer->max_tp || probe_idx == peer->max_probability ||
        // It has been probed more than anyone else
        (tx_stats.attempts_cur > peer->num_probe_cycles_done) ||
        // It will not provide higher throughput, minimum probing is enough
        ((tx_stats.perfect_tx_time > baseline_tx_time) &&
         (tx_stats.attempts_cur >= kMaxSlowProbe)) ||
        // It is almost guaranteed to fail, defer until enough cycles pass
        (IsTxUnlikely(tx_stats) &&
         (tx_stats.probe_cycles_skipped < kDeadProbeCycleCount || tx_stats.attempts_cur > 0));
    if (!skip) {
      break;
    }
  }
  if (potential_probes == 0) {
    return peer->max_tp;
  }
  ++peer->probes;
  ++peer->tx_stats_map[probe_idx].probes_total;
  return probe_idx;
}

tx_vec_idx_t GetTxVector(Peer* peer, bool needs_reliability, const ProbeSequence& probe_sequence) {
  if (needs_reliability) {
    return peer->max_probability;
  }
  if (peer->num_pkt_until_next_probe > 0) {
    --peer->num_pkt_until_next_probe;
    return peer->max_tp;
  }
  peer->num_pkt_until_next_probe = kProbeInterval - 1;
  return GetNextProbe(peer, probe_sequence);
}

tx_vec_idx_t MinstrelRateSelector::GetTxVectorIdx(const FrameControl& fc,
                                                  const common::MacAddr& peer_addr,
                                                  uint32_t flags) {
  Peer* peer = GetPeer(peer_addr);
  if (peer == nullptr) {
    return kErpStartIdx + kErpNumTxVector - 1;
  }
  if (!fc.IsData()) {
    return peer->basic_max_probability;
  }
  const bool needs_reliability = (flags & WLAN_TX_INFO_FLAGS_FAVOR_RELIABILITY) != 0;
  return GetTxVector(peer, needs_reliability, probe_sequence_);
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

Peer* MinstrelRateSelector::GetPeer(const common::MacAddr& addr) {
  auto iter = peer_map_.find(addr);
  if (iter != peer_map_.end()) {
    return &(iter->second);
  }
  return nullptr;
}

const Peer* MinstrelRateSelector::GetPeer(const common::MacAddr& addr) const {
  auto iter = peer_map_.find(addr);
  if (iter != peer_map_.end()) {
    return &(iter->second);
  }
  return nullptr;
}

zx_status_t MinstrelRateSelector::GetListToFidl(wlan_minstrel::Peers* peers_fidl) const {
  peers_fidl->peers.resize(peer_map_.size());
  size_t idx = 0;
  for (const auto& iter : peer_map_) {
    peers_fidl->peers[idx].resize(common::kMacAddrLen);
    iter.first.CopyTo(peers_fidl->peers[idx].data());
    ++idx;
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
      .probe_cycles_skipped = probe_cycles_skipped,
  };
}

zx_status_t MinstrelRateSelector::GetStatsToFidl(const common::MacAddr& peer_addr,
                                                 wlan_minstrel::Peer* peer_fidl) const {
  const auto* peer = GetPeer(peer_addr);
  if (peer == nullptr) {
    return ZX_ERR_NOT_FOUND;
  }

  peer_addr.CopyTo(peer_fidl->mac_addr.data());

  std::lock_guard<std::mutex> guard(*peer->update_lock);
  peer_fidl->entries.resize(peer->tx_stats_map.size());

  size_t idx = 0;
  for (const auto& [_, tx_stats] : peer->tx_stats_map) {
    peer_fidl->entries[idx++] = tx_stats.ToFidl();
  }
  peer_fidl->max_tp = peer->max_tp;
  peer_fidl->max_probability = peer->max_probability;
  peer_fidl->basic_highest = peer->basic_highest;
  peer_fidl->basic_max_probability = peer->basic_max_probability;
  peer_fidl->probes = peer->probes;

  return ZX_OK;
}

bool MinstrelRateSelector::IsActive() const { return next_update_ != TimeoutId{}; }

namespace debug {
// This macro requires char buf[] and size_t offset variable definitions
// in each function.
#define BUFFER(args...)                                               \
  do {                                                                \
    offset += snprintf(buf + offset, sizeof(buf) - offset, " " args); \
    if (offset >= sizeof(buf)) {                                      \
      snprintf(buf + sizeof(buf) - 12, 12, " ..(trunc)");             \
      offset = sizeof(buf);                                           \
    }                                                                 \
  } while (false)

std::string Describe(const TxStats& tx_stats) {
  char buf[256];
  size_t offset = 0;

  BUFFER("%s", Describe(tx_stats.tx_vector_idx).c_str());
  BUFFER("succ_c: %zu", tx_stats.success_cur);
  BUFFER("att_c: %zu", tx_stats.attempts_cur);
  BUFFER("succ_t: %zu", tx_stats.success_total);
  BUFFER("att_t: %zu", tx_stats.attempts_total);
  BUFFER("prob: %f", tx_stats.probability);
  BUFFER("tp: %f", tx_stats.cur_tp);
  BUFFER("probes: %zu", tx_stats.probes_total);
  BUFFER("probe_cycle_skipped: %hhu", tx_stats.probe_cycles_skipped);

  return std::string(buf, buf + offset);
}
#undef BUFFER
}  // namespace debug

}  // namespace wlan
