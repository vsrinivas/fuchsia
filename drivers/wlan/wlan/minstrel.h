// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_DRIVERS_WLAN_WLAN_MINSTREL_H_
#define GARNET_DRIVERS_WLAN_WLAN_MINSTREL_H_

#include <wlan/common/macaddr.h>
#include <wlan/common/tx_vector.h>
#include <wlan/mlme/client/station.h>
#include <wlan/mlme/timer_manager.h>
#include <wlan/protocol/info.h>

#include <fbl/unique_ptr.h>
#include <zx/time.h>

#include <fuchsia/wlan/minstrel/cpp/fidl.h>

#include <array>
#include <unordered_map>
#include <unordered_set>

namespace wlan {

static constexpr uint16_t kMinstrelFrameLength = 1400;  // bytes
static constexpr zx::duration kMinstrelUpdateInterval = zx::msec(100);
static constexpr float kMinstrelExpWeight = 0.75;  // Used to calculate moving average throughput
static constexpr float kMinstrelProbabilityThreshold =
    0.9;  // If probability is past this level, only consider throughput
static constexpr uint8_t kNumProbeSequece = 8;
static constexpr tx_vec_idx_t kSequenceLength = 1 + kMaxValidIdx - kStartIdx;

// LINT.IfChange
struct TxStats {
    tx_vec_idx_t tx_vector_idx;
    zx::duration perfect_tx_time;
    size_t success_cur = 0;   // successful transmissions since last update.
    size_t attempts_cur = 0;  // transmission attempts since last update.
    float probability =
        1.0f - kMinstrelProbabilityThreshold;  // Moving Average Probability of success.
    float cur_tp = 0.0;                        // Expected average throughput.
    size_t success_total = 0;                  // cumulative succcess counts.
    size_t attempts_total = 0;                 // cumulative attempts.

    ::fuchsia::wlan::minstrel::StatsEntry ToFidl() const;
};

struct Peer {
    common::MacAddr addr;
    bool is_ht = false;
    bool is_vht = false;

    std::unordered_map<tx_vec_idx_t, TxStats> tx_stats_map;

    // Index of the optimal tx vector
    tx_vec_idx_t max_tp = 0;           // optimality based on expected throughput.
    tx_vec_idx_t max_probability = 0;  // optimality based on success probability.
};
// LINT.ThenChange(//garnet/public/fidl/fuchsia.wlan.minstrel/wlan_minstrel.fidl)

using ProbeSequence = std::array<std::array<tx_vec_idx_t, kSequenceLength>, kNumProbeSequece>;

class MinstrelRateSelector {
   public:
    MinstrelRateSelector(TimerManager&& timer_mgr, ProbeSequence&& probe_sequence);
    void AddPeer(const wlan_assoc_ctx_t& assoc_ctx);
    void RemovePeer(const common::MacAddr& addr);
    // Called after every tx packet.
    void HandleTxStatusReport(const wlan_tx_status_t& tx_status);
    bool HandleTimeout();
    zx_status_t GetListToFidl(::fuchsia::wlan::minstrel::Peers* peers_fidl) const;
    zx_status_t GetStatsToFidl(const common::MacAddr& peer_addr,
                               ::fuchsia::wlan::minstrel::Peer* peer_fidl) const;
    bool IsActive() const;

   private:
    void UpdateStats();
    Peer* GetPeer(const common::MacAddr& addr);
    const Peer* GetPeer(const common::MacAddr& addr) const;

    // Holds MAC addresses of peers with at least one status report but has not been processed.
    std::unordered_set<common::MacAddr, common::MacAddrHasher> outdated_peers_;
    std::unordered_map<common::MacAddr, Peer, common::MacAddrHasher> peer_map_;
    TimerManager timer_mgr_;
    TimedEvent next_update_event_;

    const ProbeSequence probe_sequence_;
};

ProbeSequence RandomProbeSequence();

namespace debug {
std::string Describe(const TxStats& tx_stats);
}  // namespace debug

}  // namespace wlan

#endif  // GARNET_DRIVERS_WLAN_WLAN_MINSTREL_H_
