// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "app.h"

#include <fbl/function.h>

#include "lib/fxl/functional/make_copyable.h"
#include "lib/fxl/logging.h"

namespace bluetooth_service {

App::App(std::unique_ptr<component::ApplicationContext> application_context)
    : application_context_(std::move(application_context)),
      weak_ptr_factory_(this) {
  FXL_DCHECK(application_context_);

  manager_.set_active_adapter_changed_callback(
      fbl::BindMember(this, &App::OnActiveAdapterChanged));
  manager_.set_adapter_updated_callback(
      fbl::BindMember(this, &App::OnAdapterUpdated));
  manager_.set_adapter_removed_callback(
      fbl::BindMember(this, &App::OnAdapterRemoved));
  manager_.set_device_updated_callback(
      fbl::BindMember(this, &App::OnDeviceUpdated));

  application_context_->outgoing_services()
      ->AddService<::bluetooth_control::Control>(
          fbl::BindMember(this, &App::OnControlRequest));
  application_context_->outgoing_services()
      ->AddService<::bluetooth_low_energy::Central>(
          fbl::BindMember(this, &App::OnLowEnergyCentralRequest));
  application_context_->outgoing_services()
      ->AddService<::bluetooth_low_energy::Peripheral>(
          fbl::BindMember(this, &App::OnLowEnergyPeripheralRequest));
  application_context_->outgoing_services()
      ->AddService<::bluetooth_gatt::Server>(
          fbl::BindMember(this, &App::OnGattServerRequest));
}

void App::OnActiveAdapterChanged(
    const bluetooth_control::AdapterInfoPtr& info_ptr) {
  FXL_LOG(INFO) << "bluetooth: Active adapter changed: "
                << (info_ptr ? info_ptr->identifier : "(null)");
  if (!info_ptr) {
    for (auto& server : servers_) {
      server->NotifyActiveAdapterChanged(nullptr);
    }
    return;
  }

  for (auto& server : servers_) {
    server->NotifyActiveAdapterChanged(info_ptr);
  }
}

void App::OnAdapterUpdated(const bluetooth_control::AdapterInfoPtr& info_ptr) {
  FXL_DCHECK(info_ptr);
  FXL_LOG(INFO) << "bluetooth: Adapter changed: " << info_ptr->identifier;
  for (auto& server : servers_) {
    server->NotifyAdapterUpdated(info_ptr);
  }
}

void App::OnAdapterRemoved(const std::string& identifier) {
  FXL_LOG(INFO) << "bluetooth: Adapter removed: " << identifier;
  for (auto& server : servers_) {
    server->NotifyAdapterRemoved(identifier);
  }
}

void App::OnDeviceUpdated(const bluetooth_control::RemoteDevice& device) {
  for (auto& server : servers_) {
    server->NotifyRemoteDeviceUpdated(device);
  }
}

void App::OnControlRequest(
    ::fidl::InterfaceRequest<::bluetooth_control::Control> request) {
  auto impl = std::make_unique<ControlServer>(
      &manager_, std::move(request),
      fbl::BindMember(this, &App::OnControlServerDisconnected));
  servers_.push_back(std::move(impl));
}

void App::OnLowEnergyCentralRequest(
    ::fidl::InterfaceRequest<::bluetooth_low_energy::Central> request) {
  manager_.GetActiveAdapter(
      fxl::MakeCopyable([request = std::move(request)](auto* adapter) mutable {
        // Transfer the handle to the active adapter if there is one.
        if (adapter) {
          adapter->host()->RequestLowEnergyCentral(std::move(request));
        }
      }));
}

void App::OnLowEnergyPeripheralRequest(
    ::fidl::InterfaceRequest<::bluetooth_low_energy::Peripheral> request) {
  manager_.GetActiveAdapter(
      fxl::MakeCopyable([request = std::move(request)](auto* adapter) mutable {
        // Transfer the handle to the active adapter if there is one.
        if (adapter) {
          adapter->host()->RequestLowEnergyPeripheral(std::move(request));
        }
      }));
}

void App::OnGattServerRequest(
    ::fidl::InterfaceRequest<::bluetooth_gatt::Server> request) {
  manager_.GetActiveAdapter(
      fxl::MakeCopyable([request = std::move(request)](auto* adapter) mutable {
        // Transfer the handle to the active adapter if there is one.
        if (adapter) {
          adapter->host()->RequestGattServer(std::move(request));
        }
      }));
}

void App::OnControlServerDisconnected(ControlServer* server) {
  FXL_DCHECK(server);

  FXL_LOG(INFO) << "bluetooth: ControlServer disconnected";

  auto iter = servers_.begin();
  for (; iter != servers_.end(); ++iter) {
    if (iter->get() == server)
      break;
  }

  // An entry MUST be in the list.
  FXL_DCHECK(iter != servers_.end());
  servers_.erase(iter);
}

}  // namespace bluetooth_service
