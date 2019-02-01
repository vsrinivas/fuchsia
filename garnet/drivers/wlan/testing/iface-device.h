// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_DRIVERS_WLAN_TESTING_IFACE_DEVICE_H
#define GARNET_DRIVERS_WLAN_TESTING_IFACE_DEVICE_H

#include <ddk/device.h>
#include <wlan/protocol/mac.h>
#include <zircon/types.h>

#include <mutex>

namespace wlan {
namespace testing {

class IfaceDevice {
   public:
    IfaceDevice(zx_device_t* device, uint16_t role);

    zx_device_t* zxdev() { return zxdev_; }

    zx_status_t Bind();
    void Unbind();
    void Release();

    zx_status_t Query(uint32_t options, wlanmac_info_t* info);
    void Stop();
    zx_status_t Start(wlanmac_ifc_t* ifc, void* cookie);
    zx_status_t SetChannel(uint32_t options, wlan_channel_t* chan);

   private:
    zx_device_t* zxdev_;
    zx_device_t* parent_;

    std::mutex lock_;
    wlanmac_ifc_t* ifc_ = nullptr;
    void* ifc_cookie_ = nullptr;

    // One of the WLAN_MAC_ROLE_* constants from lib/wlan/protocol/mac.h
    uint16_t role_;
};

}  // namespace testing
}  // namespace wlan

#endif  // GARNET_DRIVERS_WLAN_TESTING_IFACE_DEVICE_H
