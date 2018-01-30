// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <wlan/mlme/bss_client_map.h>
#include <wlan/mlme/bss_interface.h>
#include <wlan/mlme/device_interface.h>
#include <wlan/mlme/frame_handler.h>
#include <wlan/mlme/macaddr_map.h>

#include <fbl/ref_ptr.h>
#include <wlan/common/macaddr.h>
#include <zircon/types.h>

namespace wlan {

class ObjectId;

// An infrastructure BSS which keeps track of its client and owned by the AP MLME.
class InfraBss : public BssInterface, public FrameHandler {
   public:
    InfraBss(DeviceInterface* device, const common::MacAddr& bssid)
        : bssid_(bssid), device_(device) {
        started_at_ = std::chrono::steady_clock::now();
    }
    virtual ~InfraBss() = default;

    zx_status_t HandleTimeout(const common::MacAddr& client_addr);

    const common::MacAddr& bssid() const override;
    uint64_t timestamp() override;
    zx_status_t AssignAid(const common::MacAddr& client, aid_t* out_aid) override;
    zx_status_t ReleaseAid(const common::MacAddr& client) override;

   private:
    zx_status_t HandleMgmtFrame(const MgmtFrameHeader& hdr) override;
    zx_status_t HandleAuthentication(const ImmutableMgmtFrame<Authentication>& frame,
                                     const wlan_rx_info_t& rxinfo) override;
    // TODO(hahnr): Remove clients when:
    // (1) the client is inactive, no traffic, for a certain duration,
    // (2) Deauthentication frames were received,
    // (3) the client timed out during authentication flow and is now idle.
    // We might be able to somehow combine (2) and (3).

    zx_status_t CreateClientTimer(const common::MacAddr& client_addr,
                                  fbl::unique_ptr<Timer>* out_timer);

    const common::MacAddr bssid_;
    DeviceInterface* device_;
    bss::timestamp_t started_at_;
    BssClientMap clients_;
};

using InfraBssMap = MacAddrMap<fbl::RefPtr<InfraBss>, macaddr_map_type::kInfraBss>;

}  // namespace wlan
