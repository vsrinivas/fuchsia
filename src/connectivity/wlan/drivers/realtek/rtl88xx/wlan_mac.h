// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be found in the LICENSE file.

#ifndef SRC_CONNECTIVITY_WLAN_DRIVERS_REALTEK_RTL88XX_WLAN_MAC_H_
#define SRC_CONNECTIVITY_WLAN_DRIVERS_REALTEK_RTL88XX_WLAN_MAC_H_

#include <wlan/protocol/phy.h>
#include <zircon/types.h>

#include <memory>

#include "bus.h"

namespace wlan {
namespace rtl88xx {

// This interface describes an instance of the MAC presented by a Realtek device.
class WlanMac {
   public:
    virtual ~WlanMac() = 0;

    // Query this WlanMac instance.
    virtual zx_status_t Query(wlanphy_info_t* info) = 0;

    // Destroy this WlanMac instance.
    virtual zx_status_t Destroy() = 0;
};

}  // namespace rtl88xx
}  // namespace wlan

#endif  // SRC_CONNECTIVITY_WLAN_DRIVERS_REALTEK_RTL88XX_WLAN_MAC_H_
