// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/scenic/lib/shutdown/lifecycle_controller_impl.h"

#include <lib/sys/cpp/component_context.h>

namespace scenic_impl {

LifecycleControllerImpl::LifecycleControllerImpl(sys::ComponentContext* app_context,
                                                 std::weak_ptr<ShutdownManager> shutdown_manager)
    : shutdown_manager_(std::move(shutdown_manager)) {
  FX_DCHECK(app_context);
  FX_DCHECK(shutdown_manager_.lock());
  app_context->outgoing()->AddPublicService(bindings_.GetHandler(this));
}

void LifecycleControllerImpl::Terminate() {
  if (auto strong = shutdown_manager_.lock()) {
    strong->Shutdown(kShutdownTimeout);
  } else {
    FX_LOGS(WARNING)
        << "LifecycleControllerImpl::Terminate(): no shutdown manager available; ignoring request.";
  }
}

}  // namespace scenic_impl
