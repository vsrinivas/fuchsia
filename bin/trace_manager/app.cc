// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/trace_manager/app.h"

#include <utility>

namespace tracing {

TraceManagerApp::TraceManagerApp(const Config& config)
    : context_(fuchsia::sys::StartupContext::CreateFromStartupInfo()),
      trace_manager_(context_.get(), config) {
  context_->outgoing().AddPublicService(
      trace_registry_bindings_.GetHandler(&trace_manager_));

  context_->outgoing().AddPublicService(
      trace_controller_bindings_.GetHandler(&trace_manager_));
}

TraceManagerApp::~TraceManagerApp() = default;

}  // namespace tracing
