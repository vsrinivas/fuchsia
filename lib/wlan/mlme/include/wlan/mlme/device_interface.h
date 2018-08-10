// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <wlan/mlme/mac_frame.h>

#include <fbl/ref_counted.h>
#include <fbl/ref_ptr.h>
#include <fbl/unique_ptr.h>
#include <wlan/common/macaddr.h>
#include <wlan/protocol/mac.h>
#include <zircon/types.h>

#include <cstdint>
#include <cstring>

namespace wlan {

class Packet;
class Timer;

// DeviceState represents the common runtime state of a device needed for interacting with external
// systems.
class DeviceState : public fbl::RefCounted<DeviceState> {
   public:
    const common::MacAddr& address() const { return addr_; }
    void set_address(const common::MacAddr& addr) { addr_ = addr; }

    wlan_channel_t channel() const { return chan_; }
    void set_channel(const wlan_channel_t& chan) { chan_ = chan; }

    bool online() { return online_; }
    void set_online(bool online) { online_ = online; }

   private:
    common::MacAddr addr_;
    wlan_channel_t chan_ = {};
    bool online_ = false;
};

// DeviceInterface represents the actions that may interact with external systems.
class DeviceInterface {
   public:
    virtual ~DeviceInterface() {}

    virtual zx_status_t GetTimer(uint64_t id, fbl::unique_ptr<Timer>* timer) = 0;

    virtual zx_status_t SendEthernet(fbl::unique_ptr<Packet> packet) = 0;
    virtual zx_status_t SendWlan(fbl::unique_ptr<Packet> packet) = 0;
    virtual zx_status_t SendService(fbl::unique_ptr<Packet> packet) = 0;

    virtual zx_status_t SetChannel(wlan_channel_t chan) = 0;
    virtual zx_status_t SetStatus(uint32_t status) = 0;
    virtual zx_status_t ConfigureBss(wlan_bss_config_t* cfg) = 0;
    virtual zx_status_t EnableBeaconing(bool enabled) = 0;
    virtual zx_status_t ConfigureBeacon(fbl::unique_ptr<Packet> packet) = 0;
    virtual zx_status_t SetKey(wlan_key_config_t* key_config) = 0;
    virtual zx_status_t StartHwScan(const wlan_hw_scan_config_t* scan_config) = 0;

    virtual fbl::RefPtr<DeviceState> GetState() = 0;
    virtual const wlanmac_info_t& GetWlanInfo() const = 0;
};

}  // namespace wlan
