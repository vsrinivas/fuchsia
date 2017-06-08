// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <apps/wlan/services/wlan_mlme.fidl-common.h>
#include <ddk/protocol/wlan.h>
#include <magenta/types.h>
#include <mxtl/unique_ptr.h>

#include <unordered_map>

namespace wlan {

class Clock;
class Packet;
struct ProbeRequest;
class Timer;

class Scanner {
  public:
    explicit Scanner(mxtl::unique_ptr<Timer> timer);

    enum class Status {
        kStartActiveScan,
        kContinueScan,
        kNextChannel,
        kFinishScan,
    };

    enum class Type {
        kPassive,
        kActive,
    };

    mx_status_t Start(ScanRequestPtr req, ScanResponsePtr resp);
    void Reset();

    bool IsRunning() const;
    Type ScanType() const;
    wlan_channel_t ScanChannel() const;

    Status HandleBeacon(const Packet* packet);
    Status HandleProbeResponse(const Packet* packet);
    Status HandleTimeout();

    mx_status_t FillProbeRequest(ProbeRequest* request, size_t len) const;
    ScanResponsePtr ScanResults();

    const Timer& timer() const { return *timer_; }

  private:
    mxtl::unique_ptr<Timer> timer_;
    ScanRequestPtr req_ = nullptr;
    ScanResponsePtr resp_ = nullptr;

    size_t channel_index_ = 0;
    mx_time_t channel_start_ = 0;

    std::unordered_map<uint64_t, BSSDescriptionPtr> bss_descriptors_;
};

}  // namespace wlan
