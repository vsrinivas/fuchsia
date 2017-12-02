// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include "beacon_sender.h"
#include "infra_bss.h"
#include "mlme.h"
#include "packet.h"

#include <ddk/protocol/wlan.h>
#include <zircon/types.h>

namespace wlan {

// ApMlme is an MLME which operates in AP mode. It is not thread-safe.
class ApMlme : public Mlme {
   public:
    explicit ApMlme(DeviceInterface* device);
    ~ApMlme() = default;

    // FrameHandler methods.
    zx_status_t HandleMlmeStartReq(const StartRequest& req) override;
    zx_status_t HandleMlmeStopReq(const StopRequest& req) override;

    // Mlme interface methods.
    zx_status_t Init() override;
    zx_status_t PreChannelChange(wlan_channel_t chan) override;
    zx_status_t PostChannelChange() override;
    zx_status_t HandleTimeout(const ObjectId id) override;

   private:
    DeviceInterface* const device_;
    fbl::unique_ptr<BeaconSender> bcn_sender_;
    InfraBssMap bss_map_;
};

}  // namespace wlan
