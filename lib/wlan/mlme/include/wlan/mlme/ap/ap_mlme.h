// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <wlan_mlme/cpp/fidl.h>
#include <wlan/mlme/ap/beacon_sender.h>
#include <wlan/mlme/ap/infra_bss.h>
#include <wlan/mlme/mlme.h>
#include <wlan/mlme/packet.h>

#include <wlan/protocol/mac.h>
#include <zircon/types.h>

namespace wlan {

// ApMlme is an MLME which operates in AP role. It is not thread-safe.
class ApMlme : public Mlme {
   public:
    explicit ApMlme(DeviceInterface* device);
    ~ApMlme();

    // FrameHandler methods.
    zx_status_t HandleMlmeStartReq(const wlan_mlme::StartRequest& req) override;
    zx_status_t HandleMlmeStopReq(const wlan_mlme::StopRequest& req) override;

    // Mlme interface methods.
    zx_status_t Init() override;
    zx_status_t PreChannelChange(wlan_channel_t chan) override;
    zx_status_t PostChannelChange() override;
    zx_status_t HandleTimeout(const ObjectId id) override;
    void HwIndication(uint32_t ind) override;

   private:
    DeviceInterface* const device_;
    fbl::RefPtr<InfraBss> bss_;
};

}  // namespace wlan
