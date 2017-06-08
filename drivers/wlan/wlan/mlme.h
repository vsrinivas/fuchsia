// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include "clock.h"
#include "scanner.h"

#include <ddktl/protocol/ethernet.h>
#include <ddktl/protocol/wlan.h>
#include <magenta/types.h>
#include <mxtl/unique_ptr.h>

namespace wlan {

class Device;
class DeviceInterface;
class Packet;

// Mlme is the Mac sub-Layer Management Entity for the wlan driver. It is not thread-safe.
class Mlme {
  public:
    Mlme(DeviceInterface* device, ddk::WlanmacProtocolProxy wlanmac_proxy);

    mx_status_t Init();
    mx_status_t Start(mxtl::unique_ptr<ddk::EthmacIfcProxy> ethmac, Device* device);
    void Stop();
    void SetServiceChannel(mx_handle_t h);

    void GetDeviceInfo(ethmac_info_t* info);

    mx_status_t HandlePacket(const Packet* packet);
    mx_status_t HandlePortPacket(uint64_t key);

  private:
    // MAC frame handlers
    mx_status_t HandleCtrlPacket(const Packet* packet);
    mx_status_t HandleDataPacket(const Packet* packet);
    mx_status_t HandleMgmtPacket(const Packet* packet);
    mx_status_t HandleSvcPacket(const Packet* packet);

    // Management frame handlers
    mx_status_t HandleBeacon(const Packet* packet);
    mx_status_t HandleProbeResponse(const Packet* packet);

    // Scan handlers
    mx_status_t StartScan(const Packet* packet);
    void HandleScanStatus(Scanner::Status status);
    mx_status_t SendScanResponse();

    DeviceInterface* device_;
    ddk::WlanmacProtocolProxy wlanmac_proxy_;
    mxtl::unique_ptr<ddk::EthmacIfcProxy> ethmac_proxy_;

    ethmac_info_t ethmac_info_ = {};

    mx_handle_t service_ = MX_HANDLE_INVALID;

    wlan_channel_t active_channel_ = { 0 };

    mxtl::unique_ptr<Scanner> scanner_;
};

}  // namespace wlan
