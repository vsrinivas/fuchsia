// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <wlan/mlme/device_interface.h>

#include "lib/wlan/fidl/wlan_mlme.fidl.h"

#include <zircon/types.h>

namespace wlan {

class BssInterface;

// Configures the driver to send Beacon frames periodically.
class BeaconSender {
   public:
    BeaconSender(DeviceInterface* device, const StartRequest& req);
    ~BeaconSender();

    void Start(BssInterface* bss);
    void Stop();

   private:
    zx_status_t WriteBeacon();
    bool IsStarted();

    DeviceInterface* const device_;
    StartRequestPtr req_;
    BssInterface* bss_;
};

}  // namespace wlan
