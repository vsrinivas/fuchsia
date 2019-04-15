// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LEDGER_BIN_FIDL_ERROR_NOTIFIER_ERROR_NOTIFIER_BINDING_H_
#define SRC_LEDGER_BIN_FIDL_ERROR_NOTIFIER_ERROR_NOTIFIER_BINDING_H_

#include <lib/fidl/cpp/binding.h>
#include <lib/fidl/cpp/enum.h>
#include <lib/fit/function.h>

#include <map>
#include <utility>

#include "src/ledger/bin/fidl/include/types.h"
#include "src/ledger/bin/sync_helper/sync_helper.h"
#include "src/lib/fxl/logging.h"

namespace ledger {

// Class for binding of FIDL interface  implementing the ErrorNotifier interface
// and using the error notifier delegate interface |D|.
// For a FIDL interface Foo, |D| is an interface named FooErrorNotifierDelegate
// that needs to be implemented by the user and passed to ErrorNotifierBinding.
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
// - Implement the |set_on_empty| method to be usable with AutoCleanableSet.
template <typename D>
class ErrorNotifierBinding {
 public:
  explicit ErrorNotifierBinding(D* delegate)
      : impl_(delegate, this), binding_(&impl_) {
    binding_.set_error_handler(
        [this](zx_status_t /* status */) { CheckEmpty(); });
    sync_helper_.set_on_empty([this] { CheckEmpty(); });
  }

  ErrorNotifierBinding(
      D* delegate, fidl::InterfaceRequest<typename D::FidlInterface> request,
      async_dispatcher_t* dispatcher = nullptr)
      : ErrorNotifierBinding(delegate) {
    binding_.Bind(std::move(request), dispatcher);
  }

  void set_on_empty(fit::closure on_empty) { on_empty_ = std::move(on_empty); }
  bool empty() { return !binding_.is_bound() && sync_helper_.empty(); }

  fidl::InterfaceRequest<typename D::FidlInterface> Unbind() {
    return binding_.Unbind();
  }
  fidl::InterfaceHandle<typename D::FidlInterface> NewBinding(
      async_dispatcher_t* dispatcher = nullptr) {
    return binding_.NewBinding(dispatcher);
  }
  void Bind(fidl::InterfaceRequest<typename D::FidlInterface> request,
            async_dispatcher_t* dispatcher = nullptr) {
    binding_.Bind(std::move(request), dispatcher);
  }

  void Close(zx_status_t status) { binding_.Close(status); }

  void Close(ledger::Status status) { Close(static_cast<zx_status_t>(status)); }

 private:
  friend typename D::Impl;

  void Sync(fit::function<void()> callback) {
    sync_helper_.RegisterSynchronizationCallback(std::move(callback));
  }

  // Wraps a callback in another one that preprends a Status arguments and
  // handles the status in case of error.
  template <typename... Args>
  auto WrapOperation(const char* function_name,
                     fit::function<void(Args...)> callback) {
    return sync_helper_.WrapOperation(
        [this, function_name, callback = std::move(callback)](
            ::fuchsia::ledger::Status status, Args&&... args) {
          if (status == ::fuchsia::ledger::Status::OK) {
            callback(std::forward<Args>(args)...);
            return;
          }
          FXL_LOG(INFO) << "FIDL call " << D::Impl::kInterfaceName
                        << "::" << function_name
                        << " failed with status: " << fidl::ToUnderlying(status)
                        << ". Sending the epitaph and closing the connection.";
          Close(status);
        });
  }

  // Returns a new callback taking a ledger::Status. This callback will be
  // responsible, in case of error, to send the status back as an event and
  // close the connection to the client.
  auto NewErrorCallback(const char* function_name) {
    return WrapOperation(function_name, fit::closure([] {}));
  }

  void CheckEmpty() {
    if (empty() && on_empty_) {
      on_empty_();
    }
  }

  typename D::Impl impl_;
  fidl::Binding<typename D::FidlInterface> binding_;
  fit::closure on_empty_;
  SyncHelper sync_helper_;
};

}  // namespace ledger

#endif  // SRC_LEDGER_BIN_FIDL_ERROR_NOTIFIER_ERROR_NOTIFIER_BINDING_H_
