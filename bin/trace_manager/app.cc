// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/trace_manager/app.h"

#include <utility>

namespace tracing {

TraceManagerApp::TraceManagerApp(const Config& config)
    : context_(component::ApplicationContext::CreateFromStartupInfo()),
      trace_manager_(context_.get(), config) {
  context_->outgoing().AddPublicService<fuchsia::tracelink::Registry>(
      [this](fidl::InterfaceRequest<fuchsia::tracelink::Registry> request) {
        trace_registry_bindings_.AddBinding(&trace_manager_,
                                            std::move(request));
      });

  context_->outgoing().AddPublicService<TraceController>(
      [this](fidl::InterfaceRequest<TraceController> request) {
        trace_controller_bindings_.AddBinding(&trace_manager_,
                                              std::move(request));
      });
}

TraceManagerApp::~TraceManagerApp() = default;

}  // namespace tracing
