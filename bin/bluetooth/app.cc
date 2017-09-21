// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "app.h"

#include "garnet/drivers/bluetooth/lib/gap/adapter.h"
#include "lib/fxl/logging.h"

using std::placeholders::_1;

namespace bluetooth_service {

App::App(std::unique_ptr<app::ApplicationContext> application_context)
    : application_context_(std::move(application_context)), weak_ptr_factory_(this) {
  FXL_DCHECK(application_context_);

  adapter_manager_.AddObserver(this);
  application_context_->outgoing_services()->AddService<::bluetooth::control::AdapterManager>(
      std::bind(&App::OnAdapterManagerRequest, this, _1));
  application_context_->outgoing_services()->AddService<::bluetooth::low_energy::Central>(
      std::bind(&App::OnLowEnergyCentralRequest, this, _1));
}

App::~App() {
  adapter_manager_.RemoveObserver(this);
}

void App::OnActiveAdapterChanged(bluetooth::gap::Adapter* adapter) {
  FXL_LOG(INFO) << "Active adapter changed: " << (adapter ? adapter->identifier() : "(null)");
  // TODO(armansito): Do something meaningful here.
}

void App::OnAdapterCreated(bluetooth::gap::Adapter* adapter) {
  FXL_LOG(INFO) << "Adapter added: " << adapter->identifier();
  // TODO(armansito): Do something meaningful here.
}

void App::OnAdapterRemoved(bluetooth::gap::Adapter* adapter) {
  FXL_LOG(INFO) << "Adapter removed: " << adapter->identifier();
  // TODO(armansito): Do something meaningful here.
}

void App::OnAdapterManagerRequest(
    ::fidl::InterfaceRequest<::bluetooth::control::AdapterManager> request) {
  auto impl = std::make_unique<AdapterManagerFidlImpl>(
      this, std::move(request), std::bind(&App::OnAdapterManagerFidlImplDisconnected, this, _1));
  adapter_manager_fidl_impls_.push_back(std::move(impl));
}

void App::OnLowEnergyCentralRequest(
    ::fidl::InterfaceRequest<::bluetooth::low_energy::Central> request) {
  auto impl = std::make_unique<LowEnergyCentralFidlImpl>(
      &adapter_manager_, std::move(request),
      std::bind(&App::OnLowEnergyCentralFidlImplDisconnected, this, _1));
  low_energy_central_fidl_impls_.push_back(std::move(impl));
}

void App::OnAdapterManagerFidlImplDisconnected(AdapterManagerFidlImpl* adapter_manager_fidl_impl) {
  FXL_DCHECK(adapter_manager_fidl_impl);

  FXL_LOG(INFO) << "AdapterManagerFidlImpl disconnected";

  auto iter = adapter_manager_fidl_impls_.begin();
  for (; iter != adapter_manager_fidl_impls_.end(); ++iter) {
    if (iter->get() == adapter_manager_fidl_impl) break;
  }

  // An entry MUST be in the list.
  FXL_DCHECK(iter != adapter_manager_fidl_impls_.end());
  adapter_manager_fidl_impls_.erase(iter);
}

void App::OnLowEnergyCentralFidlImplDisconnected(
    LowEnergyCentralFidlImpl* low_energy_central_fidl_impl) {
  FXL_CHECK(low_energy_central_fidl_impl);

  FXL_LOG(INFO) << "LowEnergyCentralFidlImpl disconnected";

  auto iter = low_energy_central_fidl_impls_.begin();
  for (; iter != low_energy_central_fidl_impls_.end(); ++iter) {
    if (iter->get() == low_energy_central_fidl_impl) break;
  }

  // An entry MUST be in the list.
  FXL_DCHECK(iter != low_energy_central_fidl_impls_.end());
  low_energy_central_fidl_impls_.erase(iter);
}

}  // namespace bluetooth_service
