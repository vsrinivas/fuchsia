// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/hardware/wlanphyimpl/cpp/banjo.h>
#include <lib/ddk/device.h>

#include <ddktl/device.h>

extern "C" {
#include "src/connectivity/wlan/drivers/third_party/intel/iwlwifi/iwl-trans.h"
}

#ifndef SRC_CONNECTIVITY_WLAN_DRIVERS_THIRD_PARTY_INTEL_IWLWIFI_DEVICE_H_
#define SRC_CONNECTIVITY_WLAN_DRIVERS_THIRD_PARTY_INTEL_IWLWIFI_DEVICE_H_

namespace wlan::iwlwifi {

class Device;
using DeviceType = ::ddk::Device<Device, ddk::Unbindable>;

class Device : public DeviceType, public ::ddk::WlanphyImplProtocol<Device, ::ddk::base_protocol> {
 public:
  virtual ~Device() = default;  // This is virtual to allow delete to happen on this class instead
                                // of the derived class.

  // Ensure derived classes implement this ::ddk::Device method to unbind and
  // free up allocated resources.
  virtual void DdkRelease() = 0;
  virtual void DdkUnbind(ddk::UnbindTxn txn) = 0;

  // WlanphyImpl interface implementation.
  zx_status_t WlanphyImplQuery(wlanphy_impl_info_t* out_info);
  zx_status_t WlanphyImplCreateIface(const wlanphy_impl_create_iface_req_t* req,
                                     uint16_t* out_iface_id);
  zx_status_t WlanphyImplDestroyIface(uint16_t iface_id);
  zx_status_t WlanphyImplSetCountry(const wlanphy_country_t* country);
  zx_status_t WlanphyImplClearCountry();
  zx_status_t WlanphyImplGetCountry(wlanphy_country_t* out_country);

  // Intended for only unit test overriding.
  void override_iwl_trans(struct iwl_trans* iwl_trans) { iwl_trans_ = iwl_trans; };

 protected:
  // Only derived classes are allowed to create this object.
  explicit Device(zx_device* parent) : DeviceType(parent){};
  struct iwl_trans* iwl_trans_;
};

}  // namespace wlan::iwlwifi

#endif  // SRC_CONNECTIVITY_WLAN_DRIVERS_THIRD_PARTY_INTEL_IWLWIFI_DEVICE_H_
