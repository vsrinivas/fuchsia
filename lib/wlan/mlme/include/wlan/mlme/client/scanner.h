// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <wlan/mlme/client/bss.h>
#include <wlan/mlme/frame_handler.h>

#include <wlan_mlme/cpp/fidl.h>

#include <fbl/unique_ptr.h>
#include <lib/zx/time.h>
#include <wlan/protocol/mac.h>
#include <zircon/types.h>

#include <unordered_map>

namespace wlan {

class Clock;
class DeviceInterface;
class Packet;
class Timer;

class Scanner : public FrameHandler {
   public:
    Scanner(DeviceInterface* device, fbl::unique_ptr<Timer> timer);
    virtual ~Scanner() {}

    enum class Type {
        kPassive,
        kActive,
    };

    zx_status_t Start(const wlan_mlme::ScanRequest& req);
    void Reset();

    bool IsRunning() const;
    Type ScanType() const;
    wlan_channel_t ScanChannel() const;

    zx_status_t HandleMlmeScanReq(const wlan_mlme::ScanRequest& req) override;

    zx_status_t HandleMgmtFrame(const MgmtFrameHeader& hdr) override;
    zx_status_t HandleBeacon(const ImmutableMgmtFrame<Beacon>& frame,
                             const wlan_rx_info_t& rxinfo) override;
    zx_status_t HandleProbeResponse(const ImmutableMgmtFrame<ProbeResponse>& frame,
                                    const wlan_rx_info_t& rxinfo) override;
    zx_status_t HandleTimeout();
    zx_status_t HandleError(zx_status_t error_code);

    const Timer& timer() const { return *timer_; }

   private:
    zx::time InitialTimeout() const;
    zx_status_t SendScanConfirm();
    zx_status_t SendProbeRequest();
    // Removes stale BSS entries from the neighbor BSS map. Pruning will only take effect every
    // kBssPruneDelay seconds, and hence, multiple calls to this method in a short time frame have
    // no effect.
    void RemoveStaleBss();
    zx_status_t ProcessBeacon(const common::MacAddr& bssid, const Beacon& bcn, uint16_t body_ie_len,
                              const wlan_rx_info_t& rxinfo);

    static constexpr size_t kMaxBssEntries = 20;  // Limited by zx.Channel buffer size.
    static constexpr zx::duration kBssExpiry = zx::sec(60);
    static constexpr zx::duration kBssPruneDelay = zx::sec(5);

    DeviceInterface* device_;
    fbl::unique_ptr<Timer> timer_;
    wlan_mlme::ScanRequestPtr req_ = nullptr;
    wlan_mlme::ScanConfirmPtr resp_ = nullptr;

    size_t channel_index_ = 0;
    zx::time channel_start_;

    // TODO(porce): Decouple neighbor BSS management from scanner.
    BssMap nbrs_bss_ = {kMaxBssEntries};
};

}  // namespace wlan
