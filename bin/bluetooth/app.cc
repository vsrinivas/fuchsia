// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "app.h"

#include <fbl/function.h>

#include "lib/fxl/functional/make_copyable.h"
#include "lib/fxl/logging.h"

namespace bluetooth_service {

App::App(std::unique_ptr<app::ApplicationContext> application_context)
    : application_context_(std::move(application_context)),
      weak_ptr_factory_(this) {
  FXL_DCHECK(application_context_);

  adapter_manager_.set_active_adapter_changed_callback(
      fbl::BindMember(this, &App::OnActiveAdapterChanged));
  adapter_manager_.set_adapter_added_callback(
      fbl::BindMember(this, &App::OnAdapterAdded));
  adapter_manager_.set_adapter_removed_callback(
      fbl::BindMember(this, &App::OnAdapterRemoved));

  application_context_->outgoing_services()
      ->AddService<::bluetooth::control::AdapterManager>(
          fbl::BindMember(this, &App::OnAdapterManagerRequest));
  application_context_->outgoing_services()
      ->AddService<::bluetooth::low_energy::Central>(
          fbl::BindMember(this, &App::OnLowEnergyCentralRequest));
  application_context_->outgoing_services()
      ->AddService<::bluetooth::low_energy::Peripheral>(
          fbl::BindMember(this, &App::OnLowEnergyPeripheralRequest));
  application_context_->outgoing_services()
      ->AddService<::bluetooth::gatt::Server>(
          fbl::BindMember(this, &App::OnGattServerRequest));
}

void App::OnActiveAdapterChanged(const Adapter* adapter) {
  FXL_LOG(INFO) << "bluetooth: Active adapter changed: "
                << (adapter ? adapter->info()->identifier : "(null)");
  for (auto& server : servers_) {
    server->NotifyActiveAdapterChanged(adapter);
  }
}

void App::OnAdapterAdded(const Adapter& adapter) {
  FXL_LOG(INFO) << "bluetooth: Adapter added: " << adapter.info()->identifier;
  for (auto& server : servers_) {
    server->NotifyAdapterAdded(adapter);
  }
}

void App::OnAdapterRemoved(const Adapter& adapter) {
  FXL_LOG(INFO) << "bluetooth: Adapter removed: " << adapter.info()->identifier;
  for (auto& server : servers_) {
    server->NotifyAdapterRemoved(adapter);
  }
}

void App::OnAdapterManagerRequest(
    ::fidl::InterfaceRequest<::bluetooth::control::AdapterManager> request) {
  auto impl = std::make_unique<AdapterManagerServer>(
      &adapter_manager_, std::move(request),
      fbl::BindMember(this, &App::OnAdapterManagerServerDisconnected));
  servers_.push_back(std::move(impl));
}

void App::OnLowEnergyCentralRequest(
    ::fidl::InterfaceRequest<::bluetooth::low_energy::Central> request) {
  adapter_manager_.GetActiveAdapter(
      fxl::MakeCopyable([request = std::move(request)](auto* adapter) mutable {
        // Transfer the handle to the active adapter if there is one.
        if (adapter) {
          adapter->host()->RequestLowEnergyCentral(std::move(request));
        }
      }));
}

void App::OnLowEnergyPeripheralRequest(
    ::fidl::InterfaceRequest<::bluetooth::low_energy::Peripheral> request) {
  adapter_manager_.GetActiveAdapter(
      fxl::MakeCopyable([request = std::move(request)](auto* adapter) mutable {
        // Transfer the handle to the active adapter if there is one.
        if (adapter) {
          adapter->host()->RequestLowEnergyPeripheral(std::move(request));
        }
      }));
}

void App::OnGattServerRequest(
    ::fidl::InterfaceRequest<::bluetooth::gatt::Server> request) {
  adapter_manager_.GetActiveAdapter(
      fxl::MakeCopyable([request = std::move(request)](auto* adapter) mutable {
        // Transfer the handle to the active adapter if there is one.
        if (adapter) {
          adapter->host()->RequestGattServer(std::move(request));
        }
      }));
}

void App::OnAdapterManagerServerDisconnected(AdapterManagerServer* server) {
  FXL_DCHECK(server);

  FXL_LOG(INFO) << "bluetooth: AdapterManagerServer disconnected";

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
