// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// TODO(https://fxbug.dev/100036): Deprecate this file when all the drivers that wlanphy driver
// binds to gets migrated to DFv2.
#ifndef SRC_CONNECTIVITY_WLAN_DRIVERS_WLANPHY_DEVICE_H_
#define SRC_CONNECTIVITY_WLAN_DRIVERS_WLANPHY_DEVICE_H_

#include <fidl/fuchsia.wlan.device/cpp/wire.h>
#include <fuchsia/hardware/wlanphyimpl/c/banjo.h>
#include <lib/ddk/device.h>
#include <lib/ddk/driver.h>
#include <lib/fidl/cpp/wire/arena.h>
#include <lib/fidl/cpp/wire/connect_service.h>

#include <ddktl/device.h>
#include <wlan/common/dispatcher.h>

namespace wlanphy {

class DeviceConnector;

class Device : public fidl::WireServer<fuchsia_wlan_device::Phy> {
 public:
  Device(zx_device_t* device, wlanphy_impl_protocol_t wlanphy_impl_proto);
  ~Device() override;

  zx_status_t Bind();
  zx_status_t Message(fidl_incoming_msg_t* msg, fidl_txn_t* txn);
  void Release();
  void Unbind();

  // Function implementations in protocol fuchsia_wlan_device::Phy.
  void GetSupportedMacRoles(GetSupportedMacRolesCompleter::Sync& completer) override;
  void CreateIface(CreateIfaceRequestView request, CreateIfaceCompleter::Sync& completer) override;
  void DestroyIface(DestroyIfaceRequestView request,
                    DestroyIfaceCompleter::Sync& completer) override;
  void SetCountry(SetCountryRequestView request, SetCountryCompleter::Sync& completer) override;
  void GetCountry(GetCountryCompleter::Sync& completer) override;
  void ClearCountry(ClearCountryCompleter::Sync& completer) override;
  void SetPsMode(SetPsModeRequestView request, SetPsModeCompleter::Sync& completer) override;
  void GetPsMode(GetPsModeCompleter::Sync& completer) override;

  zx_status_t Connect(fidl::ServerEnd<fuchsia_wlan_device::Phy> server_end);

 private:
  zx_device_t* parent_;
  zx_device_t* zxdev_;

  wlanphy_impl_protocol_t wlanphy_impl_;

  // Dispatcher for being a FIDL server listening MLME requests.
  async_dispatcher_t* server_dispatcher_;

  friend class DeviceConnector;
  friend class WlanphyConvertTest;
};

}  // namespace wlanphy

#endif  // SRC_CONNECTIVITY_WLAN_DRIVERS_WLANPHY_DEVICE_H_
