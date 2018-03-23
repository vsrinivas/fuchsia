// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "adapter_manager_server.h"

#include "lib/fxl/functional/auto_call.h"
#include "lib/fxl/functional/make_copyable.h"
#include "lib/fxl/logging.h"
#include "lib/fidl/cpp/clone.h"

#include "adapter_manager.h"

using bluetooth::Error;
using bluetooth::ErrorCode;
using bluetooth::Status;
using bluetooth_control::AdapterInfoPtr;
using bluetooth_control::AdapterManager;
using bluetooth_control::AdapterManagerDelegate;
using bluetooth_control::AdapterManagerDelegatePtr;
using bluetooth_control::AdapterManagerPtr;

namespace bluetooth_service {

AdapterManagerServer::AdapterManagerServer(
    ::bluetooth_service::AdapterManager* adapter_manager,
    ::fidl::InterfaceRequest<AdapterManager> request,
    const ConnectionErrorHandler& connection_error_handler)
    : adapter_manager_(adapter_manager),
      binding_(this, std::move(request)),
      weak_ptr_factory_(this) {
  FXL_DCHECK(adapter_manager_);
  FXL_DCHECK(connection_error_handler);
  binding_.set_error_handler(
      [this, connection_error_handler] { connection_error_handler(this); });
}

void AdapterManagerServer::NotifyActiveAdapterChanged(const Adapter* adapter) {
  if (delegate_) {
    bluetooth_control::AdapterInfoPtr info = bluetooth_control::AdapterInfo::New();
    fidl::Clone(adapter->info(), info.get());
    delegate_->OnActiveAdapterChanged(std::move(info));
  }
}

void AdapterManagerServer::NotifyAdapterAdded(const Adapter& adapter) {
  if (delegate_) {
    bluetooth_control::AdapterInfo info;
    fidl::Clone(adapter.info(), &info);
    delegate_->OnAdapterAdded(std::move(info));
  }
}

void AdapterManagerServer::NotifyAdapterRemoved(const Adapter& adapter) {
  if (delegate_) {
    delegate_->OnAdapterRemoved(adapter.info().identifier);
  }
}

void AdapterManagerServer::IsBluetoothAvailable(
    IsBluetoothAvailableCallback callback) {
  // Return true if there is an active adapter.
  auto self = weak_ptr_factory_.GetWeakPtr();
  adapter_manager_->GetActiveAdapter([self, callback](const auto* adapter) {
    if (self) {
      callback(!!adapter);
    }
  });
}

void AdapterManagerServer::SetDelegate(
    ::fidl::InterfaceHandle<AdapterManagerDelegate> delegate) {
  if (!delegate) {
    FXL_VLOG(1) << "bluetooth: Cannot assign a null delegate";
    return;
  }

  delegate_ = delegate.Bind();
  delegate_.set_error_handler([this] {
    FXL_VLOG(1) << "bluetooth: AdapterManagerDelegate disconnected";
    delegate_ = nullptr;
  });

  // Notify the delegate with a snapshot of the current adapters. We notify
  // these synchronously instead of waiting for |adapter_manager_| to be fully
  // initialized.
  for (const auto& iter : adapter_manager_->adapters()) {
    bluetooth_control::AdapterInfo info;
    fidl::Clone(iter.second->info(), &info);
    delegate_->OnAdapterAdded(std::move(info));
  }

  // Also notify the delegate of the current active adapter, if it exists.
  auto active_adapter = adapter_manager_->active_adapter();
  if (active_adapter) {
    bluetooth_control::AdapterInfoPtr info = bluetooth_control::AdapterInfo::New();
    fidl::Clone(active_adapter->info(), info.get());
    delegate_->OnActiveAdapterChanged(std::move(info));
  }
}

void AdapterManagerServer::ListAdapters(ListAdaptersCallback callback) {
  auto self = weak_ptr_factory_.GetWeakPtr();
  adapter_manager_->ListAdapters([self, callback](const auto& adapter_map) {
    if (!self)
      return;

    fidl::VectorPtr<bluetooth_control::AdapterInfo> adapters;
    for (const auto& iter : adapter_map) {
      bluetooth_control::AdapterInfo info;
      fidl::Clone(iter.second->info(), &info);
      adapters.push_back(std::move(info));
    }

    callback(std::move(adapters));
  });
}

void AdapterManagerServer::SetActiveAdapter(
    ::fidl::StringPtr identifier,
    SetActiveAdapterCallback callback) {
  bluetooth::Status status;
  auto ac =
      fxl::MakeAutoCall([&status, &callback] { callback(std::move(status)); });

  if (!adapter_manager_->SetActiveAdapter(identifier)) {
    status.error = Error::New();
    status.error->error_code = ErrorCode::NOT_FOUND;
    status.error->description = "Adapter not found";
  }
}

void AdapterManagerServer::GetActiveAdapter(
    ::fidl::InterfaceRequest<bluetooth_control::Adapter> request) {
  auto self = weak_ptr_factory_.GetWeakPtr();
  adapter_manager_->GetActiveAdapter(fxl::MakeCopyable(
      [self, request = std::move(request)](auto* adapter) mutable {
        if (!self) {
          FXL_VLOG(2) << "bluetooth: AdapterManager disconnected before active "
                         "adapter was obtained";
          return;
        }

        if (!adapter) {
          FXL_VLOG(1) << "bluetooth: no active adapter";
          return;
        }

        adapter->host()->RequestControlAdapter(std::move(request));
      }));
}

}  // namespace bluetooth_service
