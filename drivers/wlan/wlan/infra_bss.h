// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include "bss_client_map.h"
#include "device_interface.h"
#include "frame_handler.h"
#include "macaddr_map.h"

#include "garnet/drivers/wlan/common/macaddr.h"

#include <fbl/ref_ptr.h>
#include <zircon/types.h>

namespace wlan {

namespace bss {
using timestamp_t = std::chrono::time_point<std::chrono::steady_clock, std::chrono::nanoseconds>;
}

// An infrastructure BSS which keeps track of its client and owned by the AP MLME.
class InfraBss : public FrameHandler {
   public:
    InfraBss(DeviceInterface* device, const common::MacAddr& bssid)
        : bssid_(bssid), device_(device) {
        started_at_ = std::chrono::steady_clock::now();
    }
    virtual ~InfraBss() = default;

    const common::MacAddr& bssid();
    uint16_t next_seq_no();
    uint64_t timestamp();

   private:
    bool ShouldDropMgmtFrame(const MgmtFrameHeader& hdr) override;
    zx_status_t HandleAuthentication(const MgmtFrame<Authentication>& frame,
                                     const wlan_rx_info_t& rxinfo) override;
    zx_status_t HandleAssociationRequest(const MgmtFrame<AssociationRequest>& frame,
                                         const wlan_rx_info_t& rxinfo) override;
    zx_status_t SendAuthentication(const common::MacAddr& dst, status_code::StatusCode);
    zx_status_t SendAssociationResponse(const common::MacAddr& dst, status_code::StatusCode);
    // TODO(hahnr): Handle Disassocation/Deauthentication.
    // TODO(hahnr): Handle DataFrames.

    // Allocates a new Packet and fills in management header information.
    template <typename Body>
    Body* CreateMgmtFrame(const common::MacAddr& dst, ManagementSubtype subtype, size_t body_len,
                          fbl::unique_ptr<Packet>* out_packet);

    const common::MacAddr bssid_;
    DeviceInterface* device_;
    uint16_t last_seq_no_ = kMaxSequenceNumber;
    bss::timestamp_t started_at_;
    BssClientMap clients_;
};

using InfraBssMap = MacAddrMap<fbl::RefPtr<InfraBss>, macaddr_map_type::kInfraBss>;

}  // namespace wlan