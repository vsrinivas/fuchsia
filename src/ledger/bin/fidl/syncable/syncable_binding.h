// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LEDGER_BIN_FIDL_SYNCABLE_SYNCABLE_BINDING_H_
#define SRC_LEDGER_BIN_FIDL_SYNCABLE_SYNCABLE_BINDING_H_

#include <lib/fidl/cpp/binding.h>
#include <lib/fidl/cpp/enum.h>
#include <lib/fit/function.h>
#include <zircon/errors.h>

#include <map>
#include <utility>

#include "src/ledger/bin/fidl/include/types.h"
#include "src/ledger/bin/public/status.h"
#include "src/ledger/bin/sync_helper/sync_helper.h"
#include "src/ledger/lib/logging/logging.h"

namespace ledger {

// Class for binding of FIDL interface  implementing the Syncable interface
// and using the syncable delegate interface |D|.
// For a FIDL interface Foo, |D| is an interface named FooSyncableDelegate
// that needs to be implemented by the user and passed to SyncableBinding.
//
// This class internally handles the following features:
// - Implement the |Sync| method.
// - Provides a factory for passing a callback to the companion implementation
//   that will handle reporting the error and closing the connection.
// - Provides a |WrapOperation| method that needs to be called on all callback
//   before passing to the companion implementation so that |Sync| can keep
//   track of what operations are currently in progress.
//
// This class exposes the following features:
// - Access to the methods of the underlying bindings.
// - Implement the |SetOnDiscardable| method to be usable with AutoCleanableSet.
template <typename D>
class SyncableBinding {
 public:
  explicit SyncableBinding(D* delegate) : impl_(delegate, this), binding_(&impl_) {
    binding_.set_error_handler([this](zx_status_t status) {
      binding_error_status_ = status;
      CheckDiscardable();
    });
    sync_helper_.SetOnDiscardable([this] { CheckDiscardable(); });
  }

  SyncableBinding(D* delegate, fidl::InterfaceRequest<typename D::FidlInterface> request,
                  async_dispatcher_t* dispatcher = nullptr)
      : SyncableBinding(delegate) {
    binding_.Bind(std::move(request), dispatcher);
  }

  void SetOnDiscardable(fit::closure on_discardable) {
    error_handler_ = [on_discardable = std::move(on_discardable)](zx_status_t /*status*/) {
      on_discardable();
    };
  }
  void set_error_handler(fit::function<void(zx_status_t)> error_handler) {
    error_handler_ = std::move(error_handler);
  }
  bool IsDiscardable() const { return !is_bound(); }
  bool is_bound() const { return binding_.is_bound() || !sync_helper_.IsDiscardable(); };

  fidl::InterfaceRequest<typename D::FidlInterface> Unbind() { return binding_.Unbind(); }
  fidl::InterfaceHandle<typename D::FidlInterface> NewBinding(
      async_dispatcher_t* dispatcher = nullptr) {
    return binding_.NewBinding(dispatcher);
  }
  void Bind(fidl::InterfaceRequest<typename D::FidlInterface> request,
            async_dispatcher_t* dispatcher = nullptr) {
    binding_.Bind(std::move(request), dispatcher);
  }

  void Close(zx_status_t status) { binding_.Close(status); }

 private:
  friend typename D::Impl;

  void Sync(fit::function<void()> callback) {
    sync_helper_.RegisterSynchronizationCallback(std::move(callback));
  }

  // Wraps a callback in another one that preprends a Status arguments and
  // handles the status in case of error.
  template <typename... Args>
  auto WrapOperation(const char* function_name, fit::function<void(Args...)> callback) {
    return sync_helper_.WrapOperation([this, function_name, callback = std::move(callback)](
                                          ::ledger::Status status, Args&&... args) {
      if (status == ::ledger::Status::OK) {
        callback(std::forward<Args>(args)...);
        return;
      }
      LEDGER_LOG(INFO) << "FIDL call " << D::Impl::kInterfaceName << "::" << function_name
                       << " failed with status: " << status
                       << ". Sending the epitaph and closing the connection.";
      Close(ConvertToEpitaph(status));
    });
  }

  // Returns a new callback taking a ledger::Status. This callback will be
  // responsible, in case of error, to send the status back as an event and
  // close the connection to the client.
  auto NewErrorCallback(const char* function_name) {
    return WrapOperation(function_name, fit::closure([] {}));
  }

  void CheckDiscardable() {
    if (IsDiscardable() && error_handler_) {
      error_handler_(binding_error_status_);
    }
  }

  typename D::Impl impl_;
  fidl::Binding<typename D::FidlInterface> binding_;
  fit::function<void(zx_status_t)> error_handler_;
  SyncHelper sync_helper_;
  zx_status_t binding_error_status_ = ZX_ERR_PEER_CLOSED;
};

}  // namespace ledger

#endif  // SRC_LEDGER_BIN_FIDL_SYNCABLE_SYNCABLE_BINDING_H_
