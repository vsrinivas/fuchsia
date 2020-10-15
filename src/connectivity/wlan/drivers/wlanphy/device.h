// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_CONNECTIVITY_WLAN_DRIVERS_WLANPHY_DEVICE_H_
#define SRC_CONNECTIVITY_WLAN_DRIVERS_WLANPHY_DEVICE_H_

#include <fuchsia/wlan/device/cpp/fidl.h>

#include <ddk/driver.h>
#include <ddk/protocol/wlanphyimpl.h>
#include <wlan/common/dispatcher.h>

namespace wlanphy {

class DeviceConnector;

class Device : public ::fuchsia::wlan::device::Phy {
 public:
  Device(zx_device_t* device, wlanphy_impl_protocol_t wlanphy_impl_proto);
  ~Device();

  zx_status_t Bind();

  // zx_protocol_device_t
  zx_status_t Message(fidl_incoming_msg_t* msg, fidl_txn_t* txn);
  void Release();
  void Unbind();

  // wlanphy_protocol_t from ::fuchsia::wlan::device::Phy
  virtual void Query(QueryCallback callback) override;
  virtual void CreateIface(::fuchsia::wlan::device::CreateIfaceRequest req,
                           CreateIfaceCallback callback) override;
  virtual void DestroyIface(::fuchsia::wlan::device::DestroyIfaceRequest req,
                            DestroyIfaceCallback callback) override;
  virtual void SetCountry(::fuchsia::wlan::device::CountryCode req,
                          SetCountryCallback callback) override;
  virtual void GetCountry(GetCountryCallback callback) override;
  virtual void ClearCountry(ClearCountryCallback callback) override;

 private:
  zx_status_t Connect(zx::channel request);

  zx_device_t* parent_;
  zx_device_t* zxdev_;

  wlanphy_impl_protocol_t wlanphy_impl_;

  wlan::common::Dispatcher<::fuchsia::wlan::device::Phy> dispatcher_;

  friend class DeviceConnector;
};

void ConvertPhyBandInfo(::std::vector<::fuchsia::wlan::device::BandInfo>* BandInfo,
                        uint8_t bands_count, const wlan_info_band_info_t* all_phy_bands);
void ConvertPhyRolesInfo(::std::vector<::fuchsia::wlan::device::MacRole>* MacRoles,
                         wlan_info_mac_role_t mac_roles_mask);
void ConvertPhyCaps(::std::vector<::fuchsia::wlan::device::Capability>* Capabilities,
                    uint32_t phy_caps_mask);
}  // namespace wlanphy

#endif  // SRC_CONNECTIVITY_WLAN_DRIVERS_WLANPHY_DEVICE_H_
