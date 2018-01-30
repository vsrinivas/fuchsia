// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <wlan/mlme/beacon_sender_interface.h>
#include <wlan/mlme/device_interface.h>

#include "lib/wlan/fidl/wlan_mlme.fidl-common.h"

#include <zircon/types.h>

namespace wlan {

// Constructs a Beacon frame from a given MLME-START.request and sends it to the device.
// It's the driver's responsibility to pick-up the Beacon frame and configure its hardware to
// periodically send Beacon frames.
// TODO(hahnr): Add support to alter the Beacon frame later on and attach a TIM.
class HwBeaconSender : public BeaconSenderInterface {
   public:
    explicit HwBeaconSender(DeviceInterface* device);

    zx_status_t Init() override;
    bool IsStarted() override;
    zx_status_t Start(const StartRequest& req) override;
    zx_status_t Stop() override;

   private:
    zx_status_t SendBeaconFrame(const StartRequest& req);

    DeviceInterface* const device_;
    bool started_ = false;
};

}  // namespace wlan
