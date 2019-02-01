// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be found in the LICENSE file.

#ifndef GARNET_DRIVERS_WLAN_REALTEK_RTL88XX_DEVICE_H_
#define GARNET_DRIVERS_WLAN_REALTEK_RTL88XX_DEVICE_H_

#include <ddk/device.h>
#include <zircon/types.h>

#include <memory>

#include "bus.h"
#include "wlan_mac.h"

namespace wlan {
namespace rtl88xx {

// This interface describes an instance of the Realtek chipset. Implementations are specialized on
// the particular revision of the chipset they support.
class Device {
   public:
    virtual ~Device() = 0;

    // Creates and returns a WlanMac instance. Note that the returned instance is owned by a
    // zx_device_t, and thus its lifetime is managed by devhost.
    virtual zx_status_t CreateWlanMac(zx_device_t* parent_device, WlanMac** wlan_mac) = 0;

    // Factory function for Device instances. Returns an instance iff the device on `bus` is a
    // supported chipset, and the Device can be initialized correctly.
    static zx_status_t Create(std::unique_ptr<Bus> bus, std::unique_ptr<Device>* device);
};

}  // namespace rtl88xx
}  // namespace wlan

#endif  // GARNET_DRIVERS_WLAN_REALTEK_RTL88XX_DEVICE_H_
