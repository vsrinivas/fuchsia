// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "host_server.h"

#include "garnet/drivers/bluetooth/host/gatt_host.h"

#include "adapter_server.h"
#include "helpers.h"
#include "low_energy_central_server.h"
#include "low_energy_peripheral_server.h"

namespace bthost {

HostServer::HostServer(zx::channel channel,
                       fxl::WeakPtr<::btlib::gap::Adapter> adapter,
                       fbl::RefPtr<GattHost> gatt_host)
    : AdapterServerBase(adapter, this, std::move(channel)),
      gatt_host_(gatt_host) {
  FXL_DCHECK(gatt_host_);
}

void HostServer::GetInfo(const GetInfoCallback& callback) {
  callback(fidl_helpers::NewAdapterInfo(*adapter()));
}

void HostServer::RequestControlAdapter(
    f1dl::InterfaceRequest<bluetooth::control::Adapter> request) {
  BindServer<AdapterServer>(std::move(request));
}

void HostServer::RequestLowEnergyCentral(
    f1dl::InterfaceRequest<bluetooth::low_energy::Central> request) {
  BindServer<LowEnergyCentralServer>(std::move(request));
}

void HostServer::RequestLowEnergyPeripheral(
    f1dl::InterfaceRequest<bluetooth::low_energy::Peripheral> request) {
  BindServer<LowEnergyPeripheralServer>(std::move(request));
}

void HostServer::RequestGattServer(
    f1dl::InterfaceRequest<bluetooth::gatt::Server> request) {
  // GATT FIDL requests are handled by GattHost.
  gatt_host_->BindGattServer(std::move(request));
}

void HostServer::Close() {
  FXL_VLOG(1) << "bthost: Closing FIDL handles";

  // Destroy all bindings.
  servers_.clear();
  gatt_host_->CloseServers();
}

void HostServer::OnConnectionError(Server* server) {
  FXL_DCHECK(server);
  servers_.erase(server);
}

}  // namespace bthost
