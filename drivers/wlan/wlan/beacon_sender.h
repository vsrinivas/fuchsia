// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include "device_interface.h"
#include "timer.h"

#include "lib/wlan/fidl/wlan_mlme.fidl-common.h"

#include <zircon/types.h>

#include <thread>

namespace wlan {

// BeaconSender sends periodic Beacon frames for a given BSS. The BeaconSender only supports one
// BSS at a time. Beacons are sent from a separate thread.
// Sending Beacons through software is unlikely to be precise enough due to the tight time
// constraints and should be replaced with hardware support in release builds.
// However, sending Beacons through software allows postponing driver support and unblocks future
// AP development.
// TODO(hahnr): Add support for multiple BSS.
class BeaconSender {
   public:
    explicit BeaconSender(DeviceInterface* device);
    ~BeaconSender() = default;

    zx_status_t Init();
    zx_status_t Start(const StartRequest& req);
    zx_status_t Stop();

   private:
    bool IsStartedLocked() const;
    void MessageLoop();
    zx_status_t SendBeaconFrameLocked();
    zx_status_t SetTimeout();

    static constexpr uint64_t kMessageLoopMaxWaitSeconds = 30;
    // Indicates the packet was send due to the Timer being triggered.
    static constexpr uint64_t kPortPktKeyTimer = 1;
    // Message which will shut down the currently running Beacon thread.
    static constexpr uint64_t kPortPktKeyShutdown = 2;

    std::thread bcn_thread_;
    zx::port port_;
    DeviceInterface* const device_;
    fbl::unique_ptr<Timer> timer_;
    StartRequestPtr start_req_;
    std::mutex start_req_lock_;
};

}  // namespace wlan
