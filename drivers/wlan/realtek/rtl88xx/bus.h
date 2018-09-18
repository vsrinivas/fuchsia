// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be found in the LICENSE file.

#ifndef GARNET_DRIVERS_WLAN_REALTEK_RTL88XX_BUS_H_
#define GARNET_DRIVERS_WLAN_REALTEK_RTL88XX_BUS_H_

#include <ddk/device.h>
#include <zircon/types.h>

#include <memory>

namespace wlan {
namespace rtl88xx {

// This interface describes a bus, such as PCIE, USB, or SDIO, over which we can communicate with
// the hardware.
class Bus {
   public:
    virtual ~Bus() = 0;

    // Factory function for Bus instances. Returns an instance iff `bus_device` implements a
    // supported protocol, and the Bus can be constructed on that protocol.
    static zx_status_t Create(zx_device_t* bus_device, std::unique_ptr<Bus>* bus);
};

}  // namespace rtl88xx
}  // namespace wlan

#endif  // GARNET_DRIVERS_WLAN_REALTEK_RTL88XX_BUS_H_
