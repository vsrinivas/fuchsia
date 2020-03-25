// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be found in the LICENSE file.

#ifndef SRC_CONNECTIVITY_WLAN_DRIVERS_REALTEK_RTL88XX_WLAN_PHY_H_
#define SRC_CONNECTIVITY_WLAN_DRIVERS_REALTEK_RTL88XX_WLAN_PHY_H_

#include <ddk/device.h>

#ifdef __cplusplus
extern "C" {
#endif  // __cplusplus

// This implements zx_driver_ops.bind, creating a WlanPhy to manage the Realtek instance. This is
// declared with C linkage to interface with the DDK.
zx_status_t rtl88xx_bind_wlan_phy(void* ctx, zx_device_t* device);

#ifdef __cplusplus
}  // extern "C"

// The declarations below are only relevant to the C++ driver implementation.

#include <memory>

#include <ddk/protocol/wlanphyimpl.h>
#include <zircon/types.h>

#include "device.h"
#include "wlan_mac.h"

namespace wlan {
namespace rtl88xx {

// This class provides a zx_device_t implementing the ZX_PROTOCOL_WLANPHY_IMPL protocol for the
// Realtek driver. It owns the Device that implements chipset-specific logic, as well as managing
// the WlanMac instances that present the MAC interface.
class WlanPhy {
 public:
  // Factory function for WlanPhy instances. This factory returns an error code, rather than the
  // instance itself, since the instance is owned by the zx_device_t it creates and thus its
  // lifetime is managed by the devhost.
  static zx_status_t Create(zx_device_t* bus_device);
  ~WlanPhy();

 private:
  WlanPhy();
  WlanPhy(const WlanPhy& other) = delete;
  WlanPhy(WlanPhy&& other) = delete;
  WlanPhy& operator=(WlanPhy other) = delete;

  // zx_protocol_device_t implementation.
  void Unbind();
  void Release();

  // wlanphy_impl_protocol_ops implementation.
  zx_status_t Query(wlanphy_impl_info_t* info);
  zx_status_t CreateIface(const wlanphy_impl_create_iface_req_t* req, uint16_t* out_iface_id);
  zx_status_t DestroyIface(uint16_t id);
  zx_status_t SetCountry(const wlanphy_country_t* country);
  zx_status_t GetCountry(wlanphy_country_t* out_country);

  std::unique_ptr<Device> device_;
  // `zx_device_` owns this WlanPhy instance; thus this pointer is unowned.
  zx_device_t* zx_device_;
  // The lifetime of `wlan_mac_` is managed by the devhost; thus this pointer is unowned.
  WlanMac* wlan_mac_;
};

}  // namespace rtl88xx
}  // namespace wlan

#endif  // __cplusplus

#endif  // SRC_CONNECTIVITY_WLAN_DRIVERS_REALTEK_RTL88XX_WLAN_PHY_H_
