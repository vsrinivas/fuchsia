// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_RUN_TEST_COMPONENT_LOG_COLLECTOR_H_
#define GARNET_BIN_RUN_TEST_COMPONENT_LOG_COLLECTOR_H_

#include <fuchsia/logger/cpp/fidl.h>
#include <lib/fidl/cpp/binding_set.h>

#include <vector>

#include "lib/fidl/cpp/binding.h"

namespace run {

class LogCollector : public fuchsia::logger::LogListenerSafe {
 public:
  using Callback = fit::function<void(fuchsia::logger::LogMessage)>;
  explicit LogCollector(Callback callback);

  /// Bind to LogListener.
  /// Returns |ZX_ERR_ALREADY_BOUND| if already bound.
  zx_status_t Bind(fidl::InterfaceRequest<fuchsia::logger::LogListenerSafe> request,
                   async_dispatcher_t* dispatcher);

  /// Notifies once when remote channel closes.
  /// Also notifies if channel is currently unbound.
  void NotifyOnUnBind(fit::function<void()>);

 protected:
  void Log(fuchsia::logger::LogMessage log, LogCallback received) override;
  void LogMany(::std::vector<fuchsia::logger::LogMessage> logs, LogManyCallback received) override;

  void Done() override;

 private:
  Callback callback_;
  std::vector<fit::function<void()>> unbind_callbacks_;
  fidl::Binding<fuchsia::logger::LogListenerSafe> binding_;
};

}  // namespace run

#endif  // GARNET_BIN_RUN_TEST_COMPONENT_LOG_COLLECTOR_H_
