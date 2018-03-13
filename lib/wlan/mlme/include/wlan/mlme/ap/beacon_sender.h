// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <wlan/mlme/device_interface.h>

#include "lib/wlan/fidl/wlan_mlme.fidl.h"

#include <zircon/types.h>

namespace wlan {

class BssInterface;
class TrafficIndicationMap;

// Configures the driver to send Beacon frames periodically.
class BeaconSender {
   public:
    BeaconSender(DeviceInterface* device, const StartRequest& req);
    ~BeaconSender();

    void Start(BssInterface* bss);
    void Stop();
    zx_status_t UpdateBeacon(const TrafficIndicationMap& tim);

   private:
    zx_status_t WriteBeacon(const TrafficIndicationMap* tim);
    bool IsStarted();

    DeviceInterface* const device_;
    StartRequestPtr req_;
    BssInterface* bss_;
    // Buffer to write the Partial Virtual Bitmap to which was derived from a Traffic Indication
    // Map.
    uint8_t pvb_[TimElement::kMaxLenBmp];
};

}  // namespace wlan
