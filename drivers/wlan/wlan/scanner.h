// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include "bss.h"
#include "frame_handler.h"

#include "lib/wlan/fidl/wlan_mlme.fidl-common.h"

#include <ddk/protocol/wlan.h>
#include <fbl/unique_ptr.h>
#include <zircon/types.h>

#include <unordered_map>

namespace wlan {

class Clock;
class DeviceInterface;
class Packet;
struct ProbeRequest;
class Timer;

class Scanner : public FrameHandler {
   public:
    Scanner(DeviceInterface* device, fbl::unique_ptr<Timer> timer);
    virtual ~Scanner() {}

    enum class Type {
        kPassive,
        kActive,
    };

    zx_status_t Start(const ScanRequest& req);
    void Reset();

    bool IsRunning() const;
    Type ScanType() const;
    wlan_channel_t ScanChannel() const;

    zx_status_t HandleMlmeScanReq(const ScanRequest& req) override;

    bool ShouldDropMgmtFrame(const MgmtFrameHeader& hdr) override;
    zx_status_t HandleBeacon(const MgmtFrame<Beacon>& frame, const wlan_rx_info_t& rxinfo) override;
    zx_status_t HandleProbeResponse(const MgmtFrame<ProbeResponse>& frame,
                                    const wlan_rx_info_t& rxinfo) override;
    zx_status_t HandleTimeout();
    zx_status_t HandleError(zx_status_t error_code);

    const Timer& timer() const { return *timer_; }

   private:
    zx_time_t InitialTimeout() const;
    zx_status_t SendScanResponse();
    zx_status_t SendProbeRequest();
    // Removes stale BSS entries from the neighbor BSS map. Pruning will only take effect every
    // kBssPruneDelay seconds, and hence, multiple calls to this method in a short time frame have
    // no effect.
    void RemoveStaleBss();

    static constexpr size_t kMaxBssEntries = 20;  // Limited by zx.Channel buffer size.
    static constexpr zx_time_t kBssExpiry = ZX_SEC(60);
    static constexpr zx_time_t kBssPruneDelay = ZX_SEC(5);

    DeviceInterface* device_;
    fbl::unique_ptr<Timer> timer_;
    ScanRequestPtr req_;
    ScanResponsePtr resp_ = nullptr;

    size_t channel_index_ = 0;
    zx_time_t channel_start_ = 0;

    // TODO(porce): Decouple neighbor BSS management from scanner.
    BssMap nbrs_bss_ = {kMaxBssEntries};
};

}  // namespace wlan
