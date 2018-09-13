// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_DRIVERS_WLAN_WLAN_MINSTREL_H_
#define GARNET_DRIVERS_WLAN_WLAN_MINSTREL_H_

#include <wlan/common/macaddr.h>
#include <wlan/mlme/client/station.h>
#include <wlan/mlme/timer_manager.h>
#include <wlan/protocol/info.h>

#include <fbl/unique_ptr.h>
#include <zx/time.h>

#include <fuchsia/wlan/mlme/cpp/fidl.h>

#include <unordered_map>
#include <vector>

namespace wlan {

static constexpr uint8_t kMaxNssHt = 4;
static constexpr uint8_t kNumUniqueMcsHt = 8;
static constexpr uint8_t kMaxChanWidthHt = 40;  // MHz

static constexpr uint16_t kMinstrelFrameLength = 1400;  // bytes

struct TxParamSet {
    zx::duration perfect_tx_time;

    // The next 5 fields that defines the tx parameter set.
    PHY phy;
    uint8_t nss;  // number of sptatial streams, for HT and beyond
    GI gi;        // Guard Interval
    CBW cbw;
    uint8_t mcs_index;
};

struct TxStats {
    size_t success = 0;       // successful transmissions since last update.
    size_t attempts = 0;      // total transmission attempts since last update.
    float probability = 1.0;  // Moving Average Probability of success.
};

struct Peer {
    common::MacAddr addr;
    bool is_ht = false;
    bool is_vht = false;

    bool has_update = false;                 // has new transmission since last update.
    std::vector<TxParamSet> tx_params_list;  // Only need to be set once at association.
    std::vector<TxStats> tx_stats_list;      // 1:1 mapping to tx_params_list, constantly updated.
};

class MinstrelRateSelector {
   public:
    MinstrelRateSelector(TimerManager&& timer_mgr);
    void AddPeer(const wlan_assoc_ctx_t& assoc_ctx);
    void RemovePeer(const common::MacAddr& addr);
    // Called after every tx packet.
    void HandleTxStatusReport(const wlan_tx_status_t& tx_status);
    void UpdateStats();  // Driven by timer at every interval, aggregate the tx reports.

   private:
    void GenerateProbeSequence();
    Peer* GetPeer(const common::MacAddr& addr);

    std::unordered_map<common::MacAddr, Peer, common::MacAddrHasher> peer_map_;
    TimerManager timer_mgr_;
};
}  // namespace wlan

#endif  // GARNET_DRIVERS_WLAN_WLAN_MINSTREL_H_
