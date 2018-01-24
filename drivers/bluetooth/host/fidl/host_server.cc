// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "host_server.h"

#include "adapter_server.h"
#include "gatt_server_server.h"
#include "helpers.h"
#include "low_energy_central_server.h"
#include "low_energy_peripheral_server.h"

namespace bthost {

HostServer::HostServer(zx::channel channel,
                       fxl::WeakPtr<::btlib::gap::Adapter> adapter)
    : ServerBase(adapter, this, std::move(channel)) {}

void HostServer::GetInfo(const GetInfoCallback& callback) {
  callback(fidl_helpers::NewAdapterInfo(*adapter()));
}

void HostServer::RequestControlAdapter(
    fidl::InterfaceRequest<bluetooth::control::Adapter> request) {
  BindServer<AdapterServer>(std::move(request));
}

void HostServer::RequestLowEnergyCentral(
    fidl::InterfaceRequest<bluetooth::low_energy::Central> request) {
  BindServer<LowEnergyCentralServer>(std::move(request));
}

void HostServer::RequestLowEnergyPeripheral(
    fidl::InterfaceRequest<bluetooth::low_energy::Peripheral> request) {
  BindServer<LowEnergyPeripheralServer>(std::move(request));
}

void HostServer::RequestGattServer(
    fidl::InterfaceRequest<bluetooth::gatt::Server> request) {
  BindServer<GattServerServer>(std::move(request));
}

void HostServer::Close() {
  // Destroy all bindings.
  servers_.clear();
}

void HostServer::OnConnectionError(Server* server) {
  FXL_DCHECK(server);
  servers_.erase(server);
}

}  // namespace bthost
