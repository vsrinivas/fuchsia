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

#include <fuchsia/wlan/mlme/cpp/fidl.h>

#include <unordered_map>
#include <unordered_set>

namespace wlan {

static constexpr uint16_t kMinstrelFrameLength = 1400;  // bytes
static constexpr zx::duration kMinstrelUpdateInterval = zx::msec(100);

struct TxStats {
    tx_vec_idx_t tx_vector_idx;
    zx::duration perfect_tx_time;
    size_t success = 0;       // successful transmissions since last update.
    size_t attempts = 0;      // total transmission attempts since last update.
    float probability = 1.0;  // Moving Average Probability of success.
};

struct Peer {
    common::MacAddr addr;
    bool is_ht = false;
    bool is_vht = false;

    std::unordered_map<tx_vec_idx_t, TxStats>
        tx_stats_map;  // 1:1 mapping to tx_params_list, constantly updated.
};

class MinstrelRateSelector {
   public:
    MinstrelRateSelector(TimerManager&& timer_mgr);
    void AddPeer(const wlan_assoc_ctx_t& assoc_ctx);
    void RemovePeer(const common::MacAddr& addr);
    // Called after every tx packet.
    void HandleTxStatusReport(const wlan_tx_status_t& tx_status);
    void HandleTimeout();

   private:
    void GenerateProbeSequence();
    void UpdateStats();
    Peer* GetPeer(const common::MacAddr& addr);

    // Holds MAC addresses of peers with at least one status report but has not been processed.
    std::unordered_set<common::MacAddr, common::MacAddrHasher> outdated_peers_;
    std::unordered_map<common::MacAddr, Peer, common::MacAddrHasher> peer_map_;
    TimerManager timer_mgr_;
    TimedEvent next_update_event_;
};
}  // namespace wlan

#endif  // GARNET_DRIVERS_WLAN_WLAN_MINSTREL_H_
