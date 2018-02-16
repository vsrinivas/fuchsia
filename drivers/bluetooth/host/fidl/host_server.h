// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <memory>
#include <unordered_map>

#include <zx/channel.h>

#include "lib/fidl/cpp/bindings/binding.h"
#include "lib/fxl/macros.h"
#include "lib/fxl/memory/weak_ptr.h"

#include "garnet/drivers/bluetooth/host/fidl/server_base.h"
#include "garnet/drivers/bluetooth/lib/gap/adapter.h"
#include "garnet/lib/bluetooth/fidl/host.fidl.h"

namespace bthost {

class GattHost;

// Implements the Host FIDL interface. Owns all FIDL connections that have been
// opened through it.
class HostServer : public AdapterServerBase<::bluetooth::host::Host> {
 public:
  HostServer(zx::channel channel,
             fxl::WeakPtr<btlib::gap::Adapter> adapter,
             fbl::RefPtr<GattHost> gatt_host);
  ~HostServer() override = default;

 private:
  // ::bluetooth::host::Host overrides:
  void GetInfo(const GetInfoCallback& callback) override;
  void RequestControlAdapter(
      ::f1dl::InterfaceRequest<bluetooth::control::Adapter> adapter) override;
  void RequestLowEnergyCentral(
      ::f1dl::InterfaceRequest<bluetooth::low_energy::Central> central)
      override;
  void RequestLowEnergyPeripheral(
      ::f1dl::InterfaceRequest<bluetooth::low_energy::Peripheral> peripheral)
      override;
  void RequestGattServer(
      ::f1dl::InterfaceRequest<bluetooth::gatt::Server> server) override;
  void Close() override;

  // Called when |server| receives a channel connection error.
  void OnConnectionError(Server* server);

  // Helper for binding a f1dl::InterfaceRequest to a FIDL server of type
  // ServerType.
  template <typename ServerType, typename InterfaceType>
  void BindServer(f1dl::InterfaceRequest<InterfaceType> request) {
    auto server = std::make_unique<ServerType>(adapter()->AsWeakPtr(),
                                               std::move(request));
    server->set_error_handler(
        std::bind(&HostServer::OnConnectionError, this, server.get()));
    servers_[server.get()] = std::move(server);
  }

  // We hold a reference to GattHost for dispatching GATT FIDL requests.
  fbl::RefPtr<GattHost> gatt_host_;

  // All active FIDL interface servers.
  // NOTE: Each key is a raw pointer that is owned by the corresponding value.
  // This allows us to create a set of managed objects that can be looked up via
  // raw pointer.
  std::unordered_map<Server*, std::unique_ptr<Server>> servers_;

  FXL_DISALLOW_COPY_AND_ASSIGN(HostServer);
};

}  // namespace bthost
