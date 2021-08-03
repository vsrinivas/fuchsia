// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_CONNECTIVITY_WLAN_DRIVERS_THIRD_PARTY_INTEL_IWLWIFI_PLATFORM_WLANPHY_IMPL_DEVICE_H_
#define SRC_CONNECTIVITY_WLAN_DRIVERS_THIRD_PARTY_INTEL_IWLWIFI_PLATFORM_WLANPHY_IMPL_DEVICE_H_

#include <fuchsia/hardware/wlanphyimpl/cpp/banjo.h>
#include <lib/ddk/device.h>

#include <ddktl/device.h>

struct iwl_trans;

namespace wlan::iwlwifi {

class WlanphyImplDevice
    : public ::ddk::Device<WlanphyImplDevice, ::ddk::Initializable, ::ddk::Unbindable>,
      public ::ddk::WlanphyImplProtocol<WlanphyImplDevice, ::ddk::base_protocol> {
 public:
  WlanphyImplDevice(const WlanphyImplDevice& device) = delete;
  WlanphyImplDevice& operator=(const WlanphyImplDevice& other) = delete;
  virtual ~WlanphyImplDevice();

  // ::ddk::Device functions implemented by this class.
  void DdkRelease();

  // ::ddk::Device functions for initialization and unbinding, to be implemented by derived classes.
  virtual void DdkInit(::ddk::InitTxn txn) = 0;
  virtual void DdkUnbind(::ddk::UnbindTxn txn) = 0;

  // State accessors.
  virtual iwl_trans* drvdata() = 0;
  virtual const iwl_trans* drvdata() const = 0;

  // WlanphyImpl interface implementation.
  zx_status_t WlanphyImplQuery(wlanphy_impl_info_t* out_info);
  zx_status_t WlanphyImplCreateIface(const wlanphy_impl_create_iface_req_t* req,
                                     uint16_t* out_iface_id);
  zx_status_t WlanphyImplDestroyIface(uint16_t iface_id);
  zx_status_t WlanphyImplSetCountry(const wlanphy_country_t* country);
  zx_status_t WlanphyImplClearCountry();
  zx_status_t WlanphyImplGetCountry(wlanphy_country_t* out_country);

 protected:
  // Only derived classes are allowed to create this object.
  explicit WlanphyImplDevice(zx_device_t* parent);
};

}  // namespace wlan::iwlwifi

#endif  // SRC_CONNECTIVITY_WLAN_DRIVERS_THIRD_PARTY_INTEL_IWLWIFI_PLATFORM_WLANPHY_IMPL_DEVICE_H_
