// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "adapter_manager_fidl_impl.h"

#include "lib/ftl/functional/auto_call.h"
#include "lib/ftl/logging.h"

#include "app.h"
#include "fidl_helpers.h"

namespace bt_control = ::bluetooth::control;
namespace bt_fidl = ::bluetooth;

namespace bluetooth_service {

AdapterManagerFidlImpl::AdapterManagerFidlImpl(
    App* app, ::fidl::InterfaceRequest<::bt_control::AdapterManager> request,
    const ConnectionErrorHandler& connection_error_handler)
    : app_(app), binding_(this, std::move(request)) {
  FTL_DCHECK(app_);
  FTL_DCHECK(connection_error_handler);
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
    ::fidl::InterfaceHandle<::bt_control::AdapterManagerDelegate> delegate) {
  delegate_ = ::bt_control::AdapterManagerDelegatePtr::Create(std::move(delegate));
}

void AdapterManagerFidlImpl::GetAdapters(const GetAdaptersCallback& callback) {
  ::fidl::Array<::bt_control::AdapterInfoPtr> adapters;
  app_->adapter_manager()->ForEachAdapter(
      [&adapters](auto* adapter) { adapters.push_back(fidl_helpers::NewAdapterInfo(*adapter)); });

  callback(std::move(adapters));
}

void AdapterManagerFidlImpl::GetAdapter(const ::fidl::String& identifier,
                                        ::fidl::InterfaceRequest<::bt_control::Adapter> request) {
  auto adapter = app_->adapter_manager()->GetAdapter(identifier.get());
  if (adapter) {
    CreateAdapterFidlImpl(adapter, std::move(request));
  } else {
    FTL_LOG(WARNING) << "Adapter not found: " << identifier;
  }
}

void AdapterManagerFidlImpl::SetActiveAdapter(const ::fidl::String& identifier,
                                              const SetActiveAdapterCallback& callback) {
  auto status = bt_fidl::Status::New();
  auto ac = ftl::MakeAutoCall([&status, &callback] { callback(std::move(status)); });

  if (!app_->adapter_manager()->SetActiveAdapter(identifier)) {
    status->error = bt_fidl::Error::New();
    status->error->error_code = bt_fidl::ErrorCode::NOT_FOUND;
    status->error->description = "Adapter not found";
  }
}

void AdapterManagerFidlImpl::GetActiveAdapter(
    ::fidl::InterfaceRequest<::bt_control::Adapter> request) {
  auto adapter = app_->adapter_manager()->GetActiveAdapter();
  if (adapter) {
    CreateAdapterFidlImpl(adapter, std::move(request));
  } else {
    FTL_LOG(WARNING) << "No active adapter";
  }
}

void AdapterManagerFidlImpl::OnActiveAdapterChanged(bluetooth::gap::Adapter* adapter) {
  if (!delegate_) return;

  ::bt_control::AdapterInfoPtr adapter_info;
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
  FTL_DCHECK(adapter_fidl_impl);

  auto iter = adapter_fidl_impls_.begin();
  for (; iter != adapter_fidl_impls_.end(); ++iter) {
    if (iter->get() == adapter_fidl_impl) break;
  }

  // An entry MUST be in the list.
  FTL_DCHECK(iter != adapter_fidl_impls_.end());
  adapter_fidl_impls_.erase(iter);
}

void AdapterManagerFidlImpl::CreateAdapterFidlImpl(
    ftl::WeakPtr<bluetooth::gap::Adapter> adapter,
    ::fidl::InterfaceRequest<::bluetooth::control::Adapter> request) {
  FTL_DCHECK(adapter);

  auto impl = std::make_unique<AdapterFidlImpl>(
      adapter, std::move(request),
      std::bind(&AdapterManagerFidlImpl::OnAdapterFidlImplDisconnected, this,
                std::placeholders::_1));
  adapter_fidl_impls_.push_back(std::move(impl));
}

}  // namespace bluetooth_service
