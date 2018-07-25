// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <fbl/unique_ptr.h>
#include <fuchsia/wlan/mlme/c/fidl.h>
#include <fuchsia/wlan/mlme/cpp/fidl.h>
#include <fuchsia/wlan/stats/cpp/fidl.h>
#include <wlan/common/stats.h>
#include <wlan/mlme/mac_frame.h>
#include <wlan/mlme/mlme.h>
#include <wlan/mlme/service.h>
#include <wlan/protocol/mac.h>
#include <zircon/types.h>

namespace wlan {

// The Dispatcher converts Packets, forwarded by the Device, into concrete frames, such as
// management frames, or service messages.

class Dispatcher {
   public:
    explicit Dispatcher(DeviceInterface* device, fbl::unique_ptr<Mlme> mlme);
    ~Dispatcher();

    zx_status_t HandlePacket(fbl::unique_ptr<Packet>);
    zx_status_t HandlePortPacket(uint64_t key);

    // Called before a channel change happens.
    zx_status_t PreChannelChange(wlan_channel_t chan);
    // Called after a channel change is complete. The DeviceState channel will reflect the channel,
    // whether it changed or not.
    zx_status_t PostChannelChange();
    // Called when the hardware reports an indication such as Pre-TBTT.
    void HwIndication(uint32_t ind);
    void ResetStats();
    ::fuchsia::wlan::mlme::StatsQueryResponse GetStatsToFidl() const;

   private:
    zx_status_t HandleSvcPacket(fbl::unique_ptr<Packet> packet);
    template <typename Message>
    zx_status_t HandleMlmeMessage(fbl::unique_ptr<Packet> packet, uint32_t ordinal);
    zx_status_t HandleDeviceQueryRequest();
    zx_status_t HandleMlmeStats(uint32_t ordinal) const;
    template <typename T> zx_status_t SendServiceMessage(uint32_t ordinal, T* msg) const;

    DeviceInterface* device_;
    // The MLME that will handle requests for this dispatcher. This field will be set upon querying
    // the underlying DeviceInterface, based on the role of the device (e.g., Client or AP).
    fbl::unique_ptr<Mlme> mlme_ = nullptr;
    common::WlanStats<common::DispatcherStats, ::fuchsia::wlan::stats::DispatcherStats> stats_;
};

}  // namespace wlan
