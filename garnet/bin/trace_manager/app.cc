// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/trace_manager/app.h"

#include <utility>

#include <src/lib/fxl/logging.h>

namespace tracing {

TraceManagerApp::TraceManagerApp(const Config& config)
    : context_(sys::ComponentContext::Create()), trace_manager_(context_.get(), config) {
  [[maybe_unused]] zx_status_t status;

  status =
      context_->outgoing()->AddPublicService(trace_registry_bindings_.GetHandler(&trace_manager_));
  FXL_DCHECK(status == ZX_OK);

  status =
      context_->outgoing()->AddPublicService(trace_controller_bindings_.GetHandler(&trace_manager_));
  FXL_DCHECK(status == ZX_OK);

  FXL_VLOG(2) << "TraceManager services registered";
}

TraceManagerApp::~TraceManagerApp() = default;

}  // namespace tracing
