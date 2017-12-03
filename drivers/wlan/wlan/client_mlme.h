// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include "mlme.h"

#include <ddk/protocol/wlan.h>
#include <fbl/ref_ptr.h>
#include <zircon/types.h>

namespace wlan {

class DeviceInterface;
struct MgmtFrameHeader;
class Packet;
class Scanner;
class Station;

// ClientMlme is a MLME which operates in non-AP mode. It is not thread-safe.
class ClientMlme : public Mlme {
   public:
    explicit ClientMlme(DeviceInterface* device);
    ~ClientMlme();

    // Mlme interface methods.
    zx_status_t Init() override;
    zx_status_t PreChannelChange(wlan_channel_t chan) override;
    zx_status_t PostChannelChange() override;
    zx_status_t HandleTimeout(const ObjectId id) override;
    // MLME-JOIN.request will initialize a Station and starts the association flow.
    zx_status_t HandleMlmeJoinReq(const JoinRequest& msg) override;
    zx_status_t HandleMlmeDeviceQueryReq(const DeviceQueryRequest& msg) override;

    bool IsStaValid() const;

    DeviceInterface* const device_;

    fbl::RefPtr<Scanner> scanner_;
    // TODO(tkilbourn): track other STAs
    fbl::RefPtr<Station> sta_;
};

}  // namespace wlan
