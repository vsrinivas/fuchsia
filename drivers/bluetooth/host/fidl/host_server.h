// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <memory>
#include <unordered_map>

#include <fuchsia/bluetooth/control/cpp/fidl.h>
#include <fuchsia/bluetooth/host/cpp/fidl.h>
#include <lib/zx/channel.h>

#include "lib/fidl/cpp/binding.h"
#include "lib/fidl/cpp/interface_request.h"
#include "lib/fxl/macros.h"
#include "lib/fxl/memory/weak_ptr.h"

#include "garnet/drivers/bluetooth/host/fidl/server_base.h"
#include "garnet/drivers/bluetooth/lib/gap/adapter.h"
#include "garnet/drivers/bluetooth/lib/gap/bredr_connection_manager.h"
#include "garnet/drivers/bluetooth/lib/gap/bredr_discovery_manager.h"
#include "garnet/drivers/bluetooth/lib/gap/low_energy_discovery_manager.h"
#include "garnet/drivers/bluetooth/lib/gap/pairing_delegate.h"

namespace bthost {

class GattHost;

// Implements the Host FIDL interface. Owns all FIDL connections that have been
// opened through it.
class HostServer : public AdapterServerBase<fuchsia::bluetooth::host::Host>,
                   public btlib::gap::PairingDelegate {
 public:
  HostServer(zx::channel channel, fxl::WeakPtr<btlib::gap::Adapter> adapter,
             fbl::RefPtr<GattHost> gatt_host);
  ~HostServer() override = default;

 private:
  // ::fuchsia::bluetooth::Host overrides:
  void GetInfo(GetInfoCallback callback) override;
  void AddBondedDevices(
      ::fidl::VectorPtr<fuchsia::bluetooth::control::BondingData> bonds,
      AddBondedDevicesCallback callback) override;
  void SetLocalName(::fidl::StringPtr local_name,
                    SetLocalNameCallback callback) override;

  void StartDiscovery(StartDiscoveryCallback callback) override;
  void StopDiscovery(StopDiscoveryCallback callback) override;
  void SetConnectable(bool connectable,
                      SetConnectableCallback callback) override;
  void SetDiscoverable(bool discoverable,
                       SetDiscoverableCallback callback) override;

  void RequestLowEnergyCentral(
      ::fidl::InterfaceRequest<fuchsia::bluetooth::le::Central> central)
      override;
  void RequestLowEnergyPeripheral(
      ::fidl::InterfaceRequest<fuchsia::bluetooth::le::Peripheral> peripheral)
      override;
  void RequestGattServer(
      ::fidl::InterfaceRequest<fuchsia::bluetooth::gatt::Server> server)
      override;
  void SetPairingDelegate(
      ::fuchsia::bluetooth::control::InputCapabilityType input,
      ::fuchsia::bluetooth::control::OutputCapabilityType output,
      ::fidl::InterfaceHandle<::fuchsia::bluetooth::control::PairingDelegate>
          delegate) override;
  void RequestProfile(
      ::fidl::InterfaceRequest<fuchsia::bluetooth::bredr::Profile> profile)
      override;
  void Close() override;

  // ::btlib::gap::PairingDelegate overrides:
  btlib::sm::IOCapability io_capability() const override;
  void StopPairing(std::string id, btlib::sm::Status status) override;
  void ConfirmPairing(std::string id, ConfirmCallback confirm) override;
  void DisplayPasskey(std::string id, uint32_t passkey,
                      ConfirmCallback confirm) override;
  void RequestPasskey(std::string id, PasskeyResponseCallback respond) override;

  // Called by |adapter()->remote_device_cache()| when a remote device is
  // updated.
  void OnRemoteDeviceUpdated(const ::btlib::gap::RemoteDevice& remote_device);

  // Called by |adapter()->remote_device_cache()| when a remote device is
  // removed.
  void OnRemoteDeviceRemoved(const std::string& identifier);

  // Called by |adapter()->remote_device_cache()| when a remote device is
  // bonded.
  void OnRemoteDeviceBonded(const ::btlib::gap::RemoteDevice& remote_device);

  // Called when |server| receives a channel connection error.
  void OnConnectionError(Server* server);

  // Helper to start LE Discovery (called by StartDiscovery)
  void StartLEDiscovery(StartDiscoveryCallback callback);

  // Helper for binding a fidl::InterfaceRequest to a FIDL server of type
  // ServerType.
  template <typename ServerType, typename... Args>
  void BindServer(Args... args) {
    auto server = std::make_unique<ServerType>(adapter()->AsWeakPtr(),
                                               std::move(args)...);
    server->set_error_handler(
        std::bind(&HostServer::OnConnectionError, this, server.get()));
    servers_[server.get()] = std::move(server);
  }

  fuchsia::bluetooth::control::PairingDelegatePtr pairing_delegate_;

  // We hold a reference to GattHost for dispatching GATT FIDL requests.
  fbl::RefPtr<GattHost> gatt_host_;

  bool requesting_discovery_;
  std::unique_ptr<::btlib::gap::LowEnergyDiscoverySession>
      le_discovery_session_;
  std::unique_ptr<::btlib::gap::BrEdrDiscoverySession> bredr_discovery_session_;

  bool requesting_discoverable_;
  std::unique_ptr<::btlib::gap::BrEdrDiscoverableSession>
      bredr_discoverable_session_;

  btlib::sm::IOCapability io_capability_;

  // All active FIDL interface servers.
  // NOTE: Each key is a raw pointer that is owned by the corresponding value.
  // This allows us to create a set of managed objects that can be looked up via
  // raw pointer.
  std::unordered_map<Server*, std::unique_ptr<Server>> servers_;

  // Keep this as the last member to make sure that all weak pointers are
  // invalidated before other members get destroyed.
  fxl::WeakPtrFactory<HostServer> weak_ptr_factory_;

  FXL_DISALLOW_COPY_AND_ASSIGN(HostServer);
};

}  // namespace bthost
