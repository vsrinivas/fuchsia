// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "adapter_manager_fidl_impl.h"

#include "apps/bluetooth/lib/gap/low_energy_discovery_manager.h"
#include "lib/fxl/functional/auto_call.h"
#include "lib/fxl/logging.h"

#include "app.h"
#include "fidl_helpers.h"

// The internal library components and the generated FIDL bindings are both declared under the
// "bluetooth" namespace. We define an alias here to disambiguate.
namespace btfidl = ::bluetooth;

namespace bluetooth_service {

AdapterManagerFidlImpl::AdapterManagerFidlImpl(
    App* app, ::fidl::InterfaceRequest<::btfidl::control::AdapterManager> request,
    const ConnectionErrorHandler& connection_error_handler)
    : app_(app), binding_(this, std::move(request)) {
  FXL_DCHECK(app_);
  FXL_DCHECK(connection_error_handler);
  app_->adapter_manager()->AddObserver(this);
  binding_.set_connection_error_handler(
      [this, connection_error_handler] { connection_error_handler(this); });
}

AdapterManagerFidlImpl::~AdapterManagerFidlImpl() {
  app_->adapter_manager()->RemoveObserver(this);
}

void AdapterManagerFidlImpl::IsBluetoothAvailable(const IsBluetoothAvailableCallback& callback) {
  callback(app_->adapter_manager()->HasAdapters());
}

void AdapterManagerFidlImpl::SetDelegate(
    ::fidl::InterfaceHandle<::btfidl::control::AdapterManagerDelegate> delegate) {
  if (!delegate) {
    FXL_LOG(ERROR) << "Cannot set a null delegate";
    return;
  }

  delegate_ = ::btfidl::control::AdapterManagerDelegatePtr::Create(std::move(delegate));
  delegate_.set_connection_error_handler([this] {
    FXL_LOG(INFO) << "AdapterManager delegate disconnected";
    delegate_ = nullptr;
  });

  app_->adapter_manager()->ForEachAdapter(
      [this](auto* adapter) { delegate_->OnAdapterAdded(fidl_helpers::NewAdapterInfo(*adapter)); });

  // Also notify the delegate of the current active adapter, if it exists.
  auto active_adapter = app_->adapter_manager()->GetActiveAdapter();
  if (active_adapter) {
    delegate_->OnActiveAdapterChanged(fidl_helpers::NewAdapterInfo(*active_adapter));
  }
}

void AdapterManagerFidlImpl::GetAdapters(const GetAdaptersCallback& callback) {
  ::fidl::Array<::btfidl::control::AdapterInfoPtr> adapters;
  app_->adapter_manager()->ForEachAdapter(
      [&adapters](auto* adapter) { adapters.push_back(fidl_helpers::NewAdapterInfo(*adapter)); });

  callback(std::move(adapters));
}

void AdapterManagerFidlImpl::GetAdapter(
    const ::fidl::String& identifier,
    ::fidl::InterfaceRequest<::btfidl::control::Adapter> request) {
  auto adapter = app_->adapter_manager()->GetAdapter(identifier.get());
  if (adapter) {
    CreateAdapterFidlImpl(adapter, std::move(request));
  } else {
    FXL_LOG(WARNING) << "Adapter not found: " << identifier;
  }
}

void AdapterManagerFidlImpl::SetActiveAdapter(const ::fidl::String& identifier,
                                              const SetActiveAdapterCallback& callback) {
  auto status = ::btfidl::Status::New();
  auto ac = fxl::MakeAutoCall([&status, &callback] { callback(std::move(status)); });

  if (!app_->adapter_manager()->SetActiveAdapter(identifier)) {
    status->error = ::btfidl::Error::New();
    status->error->error_code = ::btfidl::ErrorCode::NOT_FOUND;
    status->error->description = "Adapter not found";
  }
}

void AdapterManagerFidlImpl::GetActiveAdapter(
    ::fidl::InterfaceRequest<::btfidl::control::Adapter> request) {
  auto adapter = app_->adapter_manager()->GetActiveAdapter();
  if (adapter) {
    CreateAdapterFidlImpl(adapter, std::move(request));
  } else {
    FXL_LOG(WARNING) << "No active adapter";
  }
}

void AdapterManagerFidlImpl::OnActiveAdapterChanged(bluetooth::gap::Adapter* adapter) {
  if (!delegate_) return;

  ::btfidl::control::AdapterInfoPtr adapter_info;
  if (adapter) adapter_info = fidl_helpers::NewAdapterInfo(*adapter);
  delegate_->OnActiveAdapterChanged(std::move(adapter_info));
}

void AdapterManagerFidlImpl::OnAdapterCreated(bluetooth::gap::Adapter* adapter) {
  if (delegate_) delegate_->OnAdapterAdded(fidl_helpers::NewAdapterInfo(*adapter));
}

void AdapterManagerFidlImpl::OnAdapterRemoved(bluetooth::gap::Adapter* adapter) {
  if (delegate_) delegate_->OnAdapterRemoved(adapter->identifier());
}

void AdapterManagerFidlImpl::OnAdapterFidlImplDisconnected(AdapterFidlImpl* adapter_fidl_impl) {
  FXL_DCHECK(adapter_fidl_impl);

  FXL_LOG(INFO) << "AdapterFidlImpl disconnected";
  auto iter = adapter_fidl_impls_.begin();
  for (; iter != adapter_fidl_impls_.end(); ++iter) {
    if (iter->get() == adapter_fidl_impl) break;
  }

  // An entry MUST be in the list.
  FXL_DCHECK(iter != adapter_fidl_impls_.end());
  adapter_fidl_impls_.erase(iter);
}

void AdapterManagerFidlImpl::CreateAdapterFidlImpl(
    fxl::WeakPtr<bluetooth::gap::Adapter> adapter,
    ::fidl::InterfaceRequest<::bluetooth::control::Adapter> request) {
  FXL_DCHECK(adapter);

  auto impl = std::make_unique<AdapterFidlImpl>(
      adapter, std::move(request),
      std::bind(&AdapterManagerFidlImpl::OnAdapterFidlImplDisconnected, this,
                std::placeholders::_1));
  adapter_fidl_impls_.push_back(std::move(impl));
}

}  // namespace bluetooth_service
