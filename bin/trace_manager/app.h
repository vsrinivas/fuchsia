// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef APPS_TRACING_SRC_TRACE_MANAGER_APP_H_
#define APPS_TRACING_SRC_TRACE_MANAGER_APP_H_

#include <memory>

#include "lib/app/cpp/application_context.h"
#include "apps/tracing/src/trace_manager/trace_manager.h"
#include "lib/fidl/cpp/bindings/binding_set.h"
#include "lib/fxl/macros.h"

namespace tracing {

class TraceManagerApp {
 public:
  explicit TraceManagerApp(const Config& config);
  ~TraceManagerApp();

 private:
  std::unique_ptr<app::ApplicationContext> context_;
  TraceManager trace_manager_;
  fidl::BindingSet<TraceRegistry> trace_registry_bindings_;
  fidl::BindingSet<TraceController> trace_controller_bindings_;

  FXL_DISALLOW_COPY_AND_ASSIGN(TraceManagerApp);
};

}  // namespace tracing

#endif  // APPS_TRACING_SRC_TRACE_MANAGER_APP_H_
