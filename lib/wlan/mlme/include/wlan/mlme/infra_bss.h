// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <wlan/mlme/bss_client_map.h>
#include <wlan/mlme/bss_interface.h>
#include <wlan/mlme/device_interface.h>
#include <wlan/mlme/frame_handler.h>
#include <wlan/mlme/macaddr_map.h>
#include <wlan/mlme/remote_client.h>

#include <fbl/ref_ptr.h>
#include <wlan/common/macaddr.h>
#include <zircon/types.h>

#include <unordered_set>

namespace wlan {

class ObjectId;

// An infrastructure BSS which keeps track of its client and owned by the AP MLME.
class InfraBss : public BssInterface, public FrameHandler, public RemoteClient::Listener {
   public:
    InfraBss(DeviceInterface* device, const common::MacAddr& bssid)
        : bssid_(bssid), device_(device) {
        started_at_ = std::chrono::steady_clock::now();
    }
    virtual ~InfraBss() = default;

    zx_status_t HandleTimeout(const common::MacAddr& client_addr);

    // BssInterface implementation
    const common::MacAddr& bssid() const override;
    uint64_t timestamp() override;
    zx_status_t AssignAid(const common::MacAddr& client, aid_t* out_aid) override;
    zx_status_t ReleaseAid(const common::MacAddr& client) override;
    fbl::unique_ptr<Buffer> GetPowerSavingBuffer(size_t len) override;

   private:
    using ClientSet = std::unordered_set<common::MacAddr, common::MacAddrHasher>;

    // FrameHandler implementation
    zx_status_t HandleDataFrame(const DataFrameHeader& hdr) override;
    zx_status_t HandleMgmtFrame(const MgmtFrameHeader& hdr) override;
    zx_status_t HandleAuthentication(const ImmutableMgmtFrame<Authentication>& frame,
                                     const wlan_rx_info_t& rxinfo) override;
    zx_status_t HandlePsPollFrame(const ImmutableCtrlFrame<PsPollFrame>& frame,
                                  const wlan_rx_info_t& rxinfo) override;

    // RemoteClient::Listener implementation
    void HandleClientStateChange(const common::MacAddr& client, RemoteClient::StateId from,
                                 RemoteClient::StateId to) override;
    void HandleClientPowerSaveMode(const common::MacAddr& client, bool dozing) override;

    zx_status_t CreateClientTimer(const common::MacAddr& client_addr,
                                  fbl::unique_ptr<Timer>* out_timer);

    const common::MacAddr bssid_;
    DeviceInterface* device_;
    bss::timestamp_t started_at_;
    BssClientMap clients_;
    ClientSet dozing_clients_;
};

using InfraBssMap = MacAddrMap<fbl::RefPtr<InfraBss>, macaddr_map_type::kInfraBss>;

}  // namespace wlan
