// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <wlan/common/macaddr.h>
#include <wlan/mlme/client/station.h>
#include <wlan/protocol/info.h>

#include <fbl/unique_ptr.h>
#include <zx/time.h>

#include <fuchsia/wlan/mlme/cpp/fidl.h>

#include <unordered_map>

namespace wlan {

struct RateStats {
};

struct RateGroup {
};

struct Peer {
};

class MinstrelRateSelector {
   public:
    MinstrelRateSelector(fbl::unique_ptr<Timer> timer);
    void AddPeer(common::MacAddr addr, const HtCapabilities& ht_cap);
    void RemovePeer(const common::MacAddr& addr);
    void HandleTxStatusReport(const wlan_tx_status_t& tx_status);  // Called after every tx packet.
    void UpdateStats(); // Driven by timer at every interval, aggregate the tx reports.

   private:
    Peer* GetPeer(const common::MacAddr& addr);

    std::unordered_map<common::MacAddr, Peer, common::MacAddrHasher> peer_map_;
    fbl::unique_ptr<Timer> timer_;
};
}  // namespace wlan
