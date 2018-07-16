// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_TRACE_MANAGER_APP_H_
#define GARNET_BIN_TRACE_MANAGER_APP_H_

#include <memory>

#include <fuchsia/tracelink/cpp/fidl.h>
#include <fuchsia/tracing/cpp/fidl.h>

#include "garnet/bin/trace_manager/trace_manager.h"
#include "lib/component/cpp/startup_context.h"
#include "lib/fidl/cpp/binding_set.h"
#include "lib/fxl/macros.h"

namespace tracing {

class TraceManagerApp {
 public:
  explicit TraceManagerApp(const Config& config);
  ~TraceManagerApp();

 private:
  std::unique_ptr<component::StartupContext> context_;
  TraceManager trace_manager_;
  fidl::BindingSet<fuchsia::tracelink::Registry> trace_registry_bindings_;
  fidl::BindingSet<fuchsia::tracing::TraceController> trace_controller_bindings_;

  FXL_DISALLOW_COPY_AND_ASSIGN(TraceManagerApp);
};

}  // namespace tracing

#endif  // GARNET_BIN_TRACE_MANAGER_APP_H_
