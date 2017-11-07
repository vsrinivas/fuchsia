// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <ddk/protocol/wlan.h>
#include <fbl/unique_ptr.h>
#include <zircon/types.h>

#include <unordered_map>

#include "bss.h"
#include "lib/wlan/fidl/wlan_mlme.fidl-common.h"

namespace wlan {

class Clock;
class DeviceInterface;
class Packet;
struct ProbeRequest;
class Timer;

class Scanner {
   public:
    Scanner(DeviceInterface* device, fbl::unique_ptr<Timer> timer);

    enum class Type {
        kPassive,
        kActive,
    };

    zx_status_t Start(ScanRequestPtr req, ScanResponsePtr resp);
    void Reset();

    bool IsRunning() const;
    Type ScanType() const;
    wlan_channel_t ScanChannel() const;

    zx_status_t HandleBeacon(const MgmtFrame<Beacon>* frame, const wlan_rx_info_t* rxinfo);
    zx_status_t HandleProbeResponse(const MgmtFrame<ProbeResponse>* frame,
                                    const wlan_rx_info_t* rxinfo);
    zx_status_t Start(ScanRequestPtr req);
    zx_status_t HandleTimeout();
    zx_status_t HandleError(zx_status_t error_code);

    const Timer& timer() const { return *timer_; }

   private:
    zx_time_t InitialTimeout() const;
    zx_status_t SendScanResponse();
    zx_status_t SendProbeRequest();

    DeviceInterface* device_;
    fbl::unique_ptr<Timer> timer_;
    ScanRequestPtr req_ = nullptr;
    ScanResponsePtr resp_ = nullptr;

    size_t channel_index_ = 0;
    zx_time_t channel_start_ = 0;

    // TODO(porce): Decouple neighbor BSS management from scanner.
    BssMap nbrs_bss_;
};

}  // namespace wlan
