// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "app.h"

#include "apps/bluetooth/lib/gap/adapter.h"
#include "lib/ftl/logging.h"

using std::placeholders::_1;

namespace bluetooth_service {

App::App(std::unique_ptr<app::ApplicationContext> application_context)
    : application_context_(std::move(application_context)), weak_ptr_factory_(this) {
  FTL_DCHECK(application_context_);

  adapter_manager_.AddObserver(this);
  application_context_->outgoing_services()->AddService<::bluetooth::control::AdapterManager>(
      std::bind(&App::OnAdapterManagerRequest, this, _1));
}

App::~App() {
  adapter_manager_.RemoveObserver(this);
}

void App::OnActiveAdapterChanged(bluetooth::gap::Adapter* adapter) {
  FTL_LOG(INFO) << "Active adapter changed: " << (adapter ? adapter->identifier() : "(null)");
  // TODO(armansito): Do something meaningful here.
}

void App::OnAdapterCreated(bluetooth::gap::Adapter* adapter) {
  FTL_LOG(INFO) << "Adapter added: " << adapter->identifier();
  // TODO(armansito): Do something meaningful here.
}

void App::OnAdapterRemoved(bluetooth::gap::Adapter* adapter) {
  FTL_LOG(INFO) << "Adapter removed: " << adapter->identifier();
  // TODO(armansito): Do something meaningful here.
}

void App::OnAdapterManagerRequest(
    ::fidl::InterfaceRequest<::bluetooth::control::AdapterManager> request) {
  auto impl = std::make_unique<AdapterManagerFidlImpl>(
      this, std::move(request), std::bind(&App::OnAdapterManagerFidlImplDisconnected, this, _1));
  adapter_manager_fidl_impls_.push_back(std::move(impl));
}

void App::OnAdapterManagerFidlImplDisconnected(AdapterManagerFidlImpl* adapter_manager_fidl_impl) {
  FTL_DCHECK(adapter_manager_fidl_impl);

  auto iter = adapter_manager_fidl_impls_.begin();
  for (; iter != adapter_manager_fidl_impls_.end(); ++iter) {
    if (iter->get() == adapter_manager_fidl_impl) break;
  }

  // An entry MUST be in the list.
  FTL_DCHECK(iter != adapter_manager_fidl_impls_.end());
  adapter_manager_fidl_impls_.erase(iter);
}

}  // namespace bluetooth_service
