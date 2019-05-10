// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be found in the LICENSE file.

#ifndef SRC_CONNECTIVITY_WLAN_DRIVERS_REALTEK_RTL88XX_USB_BUS_H_
#define SRC_CONNECTIVITY_WLAN_DRIVERS_REALTEK_RTL88XX_USB_BUS_H_

#include <memory>

#include <ddk/device.h>
#include <zircon/types.h>

#include "bus.h"

namespace wlan {
namespace rtl88xx {

// Create a Bus instance that communicates with a Realtek chip over the USB bus.
zx_status_t CreateUsbBus(zx_device_t* bus_device, std::unique_ptr<Bus>* bus);

}  // namespace rtl88xx
}  // namespace wlan

#endif  // SRC_CONNECTIVITY_WLAN_DRIVERS_REALTEK_RTL88XX_USB_BUS_H_
