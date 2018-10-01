// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PERIDOT_BIN_LEDGER_FIDL_ERROR_NOTIFIER_ERROR_NOTIFIER_PROXY_BASE_H_
#define PERIDOT_BIN_LEDGER_FIDL_ERROR_NOTIFIER_ERROR_NOTIFIER_PROXY_BASE_H_

#include <map>
#include <utility>

#include <lib/fidl/cpp/binding.h>
#include <lib/fit/function.h>
#include <lib/fxl/logging.h>

#include "peridot/bin/ledger/fidl/include/types.h"
#include "peridot/bin/ledger/sync_helper/sync_helper.h"

namespace ledger {

// Base class for implementation of FIDL interface |I| implementing the
// ErrorNotifier interface and using the error notifier delegate interface |D|.
// For a FIDL interface Foo, |D| is an interface named FooErrorNotifierDelegate
// that needs to be implemented by the user and passed to FooErrorNotifierProxy
// (also automatically generated).
//
// This base class handles the following features:
// - Implement the |Sync| method
// - Implement the |set_on_empty| method to be usable with AutoCleanableSet
// - Provides a factory for passing a callback to the companion implementation
//   that will handle reporting the error and closing the connection.
// - Provides a |WrapOperation| method that needs to be called on all callback
//   before passing to the companion implementation so that |Sync| can keep
//   track of what operations are currently in progress.
template <typename I, typename D>
class ErrorNotifierProxyBase : public I {
 protected:
  ErrorNotifierProxyBase(const char* interface_name, D* delegate,
                         fidl::InterfaceRequest<I> request)
      : delegate_(delegate),
        interface_name_(interface_name),
        binding_(this, std::move(request)) {
    binding_.set_error_handler([this](zx_status_t /* status */) {
      if (on_empty_) {
        on_empty_();
      }
    });
  }

 public:
  void set_on_empty(fit::closure on_empty) { on_empty_ = std::move(on_empty); }

 protected:
  void Sync(fit::function<void()> callback) override {
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
          FXL_LOG(INFO) << "FIDL call " << interface_name_
                        << "::" << function_name << " failed with status: "
                        << static_cast<zx_status_t>(status)
                        << ". Sending the epitaph and closing the connection.";
          binding_.Close(static_cast<zx_status_t>(status));
          binding_.Unbind();
          if (on_empty_) {
            on_empty_();
          }
        });
  }

  // Returns a new callback taking a ledger::Status. This callback will be
  // responsible, in case of error, to send the status back as an event and
  // close the connection to the client.
  auto NewErrorCallback(const char* function_name) {
    return WrapOperation(function_name, fit::closure([] {}));
  }

  D* const delegate_;

 private:
  const char* const interface_name_;
  fidl::Binding<I> binding_;
  fit::closure on_empty_;
  SyncHelper sync_helper_;
};

}  // namespace ledger

#endif  // PERIDOT_BIN_LEDGER_FIDL_ERROR_NOTIFIER_ERROR_NOTIFIER_PROXY_BASE_H_
