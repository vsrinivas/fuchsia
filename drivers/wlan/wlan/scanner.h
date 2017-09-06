// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <apps/wlan/services/wlan_mlme.fidl-common.h>
#include <ddk/protocol/wlan.h>
#include <magenta/types.h>
#include <fbl/unique_ptr.h>

#include <unordered_map>

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

    mx_status_t Start(ScanRequestPtr req, ScanResponsePtr resp);
    void Reset();

    bool IsRunning() const;
    Type ScanType() const;
    wlan_channel_t ScanChannel() const;

    mx_status_t Start(ScanRequestPtr req);
    mx_status_t HandleBeaconOrProbeResponse(const Packet* packet);
    mx_status_t HandleTimeout();
    mx_status_t HandleError(mx_status_t error_code);

    const Timer& timer() const { return *timer_; }

  private:
    mx_time_t InitialTimeout() const;
    mx_status_t SendScanResponse();
    mx_status_t SendProbeRequest();

    DeviceInterface* device_;
    fbl::unique_ptr<Timer> timer_;
    ScanRequestPtr req_ = nullptr;
    ScanResponsePtr resp_ = nullptr;

    size_t channel_index_ = 0;
    mx_time_t channel_start_ = 0;

    std::unordered_map<uint64_t, BSSDescriptionPtr> bss_descriptors_;
};

}  // namespace wlan
