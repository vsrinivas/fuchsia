// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <ddktl/protocol/ethernet.h>
#include <ddktl/protocol/wlan.h>
#include <magenta/types.h>
#include <mxtl/unique_ptr.h>

namespace wlan {

class Device;
class Packet;

// Mlme is the Mac sub-Layer Management Entity for the wlan driver. It is not thread-safe.
class Mlme {
  public:
    explicit Mlme(ddk::WlanmacProtocolProxy wlanmac_proxy);

    mx_status_t Init();
    mx_status_t Start(mxtl::unique_ptr<ddk::EthmacIfcProxy> ethmac, Device* device);
    void Stop();

    void GetDeviceInfo(ethmac_info_t* info);

    mx_status_t HandlePacket(const Packet* packet, mx_time_t* next_timeout);
    mx_status_t HandleTimeout(mx_time_t* next_timeout);

  private:
    mx_status_t HandleCtrlPacket(const Packet* packet);
    mx_status_t HandleDataPacket(const Packet* packet);
    mx_status_t HandleMgmtPacket(const Packet* packet);
    mx_status_t HandleSvcPacket(const Packet* packet);

    void SetNextTimeout(mx_time_t* next_timeout);

    ddk::WlanmacProtocolProxy wlanmac_proxy_;
    mxtl::unique_ptr<ddk::EthmacIfcProxy> ethmac_proxy_;

    ethmac_info_t ethmac_info_ = {};
};

}  // namespace wlan
