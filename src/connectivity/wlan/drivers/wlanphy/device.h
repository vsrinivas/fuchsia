// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_CONNECTIVITY_WLAN_DRIVERS_WLANPHY_DEVICE_H_
#define SRC_CONNECTIVITY_WLAN_DRIVERS_WLANPHY_DEVICE_H_

#include <fidl/fuchsia.wlan.device/cpp/wire.h>
#include <fuchsia/hardware/wlanphyimpl/c/banjo.h>
#include <lib/ddk/driver.h>

#include <wlan/common/dispatcher.h>

namespace wlanphy {

class DeviceConnector;

class Device : public fidl::WireServer<fuchsia_wlan_device::Phy> {
 public:
  Device(zx_device_t* device, wlanphy_impl_protocol_t wlanphy_impl_proto);
  ~Device() override;

  zx_status_t Bind();

  // zx_protocol_device_t
  zx_status_t Message(fidl_incoming_msg_t* msg, fidl_txn_t* txn);
  void Release();
  void Unbind();

  // wlanphy_protocol_t from fuchsia_wlan_device::Phy
  void GetSupportedMacRoles(GetSupportedMacRolesRequestView req,
                            GetSupportedMacRolesCompleter::Sync& completer) override;
  void CreateIface(CreateIfaceRequestView req, CreateIfaceCompleter::Sync& completer) override;
  void DestroyIface(DestroyIfaceRequestView req, DestroyIfaceCompleter::Sync& completer) override;
  void SetCountry(SetCountryRequestView req, SetCountryCompleter::Sync& completer) override;
  void GetCountry(GetCountryRequestView req, GetCountryCompleter::Sync& completer) override;
  void ClearCountry(ClearCountryRequestView req, ClearCountryCompleter::Sync& completer) override;
  void SetPsMode(SetPsModeRequestView req, SetPsModeCompleter::Sync& completer) override;
  void GetPsMode(GetPsModeRequestView req, GetPsModeCompleter::Sync& completer) override;

 private:
  zx_status_t Connect(fidl::ServerEnd<fuchsia_wlan_device::Phy> server_end);

  zx_device_t* parent_;
  zx_device_t* zxdev_;

  wlanphy_impl_protocol_t wlanphy_impl_;

  wlan::common::Dispatcher<fuchsia_wlan_device::Phy> dispatcher_;

  friend class DeviceConnector;
};

}  // namespace wlanphy

#endif  // SRC_CONNECTIVITY_WLAN_DRIVERS_WLANPHY_DEVICE_H_
