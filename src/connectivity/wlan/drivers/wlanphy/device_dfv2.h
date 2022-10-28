// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_CONNECTIVITY_WLAN_DRIVERS_WLANPHY_DEVICE_DFV2_H_
#define SRC_CONNECTIVITY_WLAN_DRIVERS_WLANPHY_DEVICE_DFV2_H_

#include <fidl/fuchsia.wlan.device/cpp/wire.h>
#include <fidl/fuchsia.wlan.wlanphyimpl/cpp/driver/wire.h>
#include <fidl/fuchsia.wlan.wlanphyimpl/cpp/wire.h>
#include <fuchsia/hardware/wlanphyimpl/c/banjo.h>
#include <lib/ddk/device.h>
#include <lib/ddk/driver.h>
#include <lib/fdf/cpp/channel_read.h>
#include <lib/fdf/cpp/dispatcher.h>
#include <lib/fidl/cpp/wire/arena.h>
#include <lib/fidl/cpp/wire/connect_service.h>

#include <ddktl/device.h>
#include <wlan/common/dispatcher.h>

namespace wlanphy {

class DeviceConnector;

class Device
    : public fidl::WireServer<fuchsia_wlan_device::Phy>,
      public ::ddk::Device<Device, ::ddk::Messageable<fuchsia_wlan_device::Connector>::Mixin,
                           ::ddk::Unbindable> {
 public:
  Device(zx_device_t* device, fdf::ClientEnd<fuchsia_wlan_wlanphyimpl::WlanphyImpl> client);
  ~Device();

  // Creates and binds wlanphy device instance. On success hands device off to device lifecycle
  // management.
  static zx_status_t Create(zx_device_t* parent_device,
                            fdf::ClientEnd<fuchsia_wlan_wlanphyimpl::WlanphyImpl> client);

  // Overriding DDK functions.
  void DdkRelease();
  void DdkUnbind(::ddk::UnbindTxn txn);

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

  // Function implementations in protocol fuchsia_wlan_device::Connector.
  void Connect(ConnectRequestView request, ConnectCompleter::Sync& completer) override;
  void Connect(fidl::ServerEnd<fuchsia_wlan_device::Phy> server_end);

  zx_status_t ConnectToWlanphyImpl(fdf::Channel server_channel);

  // Add the device to the devhost.
  zx_status_t DeviceAdd();

 private:
  // Dispatcher for being a FIDL server listening MLME requests.
  async_dispatcher_t* server_dispatcher_;

  // The FIDL client to communicate with iwlwifi
  fdf::WireSharedClient<fuchsia_wlan_wlanphyimpl::WlanphyImpl> client_;

  // Dispatcher for being a FIDL client firing requests to WlanphyImpl device.
  fdf::Dispatcher client_dispatcher_;

  // Store unbind txn for async reply.
  std::optional<::ddk::UnbindTxn> unbind_txn_;

  friend class DeviceConnector;
};

}  // namespace wlanphy

#endif  // SRC_CONNECTIVITY_WLAN_DRIVERS_WLANPHY_DEVICE_DFV2_H_
