// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be found in the LICENSE file.

#ifndef SRC_CONNECTIVITY_WLAN_DRIVERS_REALTEK_RTL88XX_RTL8821C_DEVICE_H_
#define SRC_CONNECTIVITY_WLAN_DRIVERS_REALTEK_RTL88XX_RTL8821C_DEVICE_H_

#include <memory>

#include "bus.h"
#include "device.h"
#include "wlan_mac.h"

namespace wlan {
namespace rtl88xx {

// This implementation of the Device interface supports the Realtek 8821C chipset.
class Rtl8821cDevice : public Device {
   public:
    // Factory function for Rtl8821cDevice instances.
    static zx_status_t Create(std::unique_ptr<Bus> bus, std::unique_ptr<Device>* device);
    ~Rtl8821cDevice() override;

    // Device implementation.
    zx_status_t CreateWlanMac(zx_device_t* parent_device, WlanMac** wlan_mac) override;

   private:
    Rtl8821cDevice();
    Rtl8821cDevice(const Rtl8821cDevice& other) = delete;
    Rtl8821cDevice(Rtl8821cDevice&& other) = delete;
    Rtl8821cDevice& operator=(Rtl8821cDevice other) = delete;

    // Halmac-style configuration functions.
    zx_status_t PreInitSystemCfg88xx();

    std::unique_ptr<Bus> bus_;
};

}  // namespace rtl88xx
}  // namespace wlan

#endif  // SRC_CONNECTIVITY_WLAN_DRIVERS_REALTEK_RTL88XX_RTL8821C_DEVICE_H_
