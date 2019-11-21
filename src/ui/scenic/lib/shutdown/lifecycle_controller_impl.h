// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_SCENIC_LIB_SHUTDOWN_LIFECYCLE_CONTROLLER_IMPL_H_
#define SRC_UI_SCENIC_LIB_SHUTDOWN_LIFECYCLE_CONTROLLER_IMPL_H_

#include <memory>

#include <fuchsia/ui/lifecycle/cpp/fidl.h>
#include <lib/fidl/cpp/binding_set.h>

#include "src/ui/scenic/lib/shutdown/shutdown_manager.h"

namespace sys {
class ComponentContext;
}  // namespace sys

namespace scenic_impl {

// Implements the FIDL LifecycleController API.  Delegates shutdown activities to the
// ShutdownManager that is passed into the constructor.
class LifecycleControllerImpl : public fuchsia::ui::lifecycle::LifecycleController {
 public:
  static constexpr zx::duration kShutdownTimeout{1000000000};  // 1 second

  // |app_context| is used to publish this service.
  // |shutdown_manager| is used to actually perform the shutdown.
  LifecycleControllerImpl(sys::ComponentContext* app_context,
                          std::weak_ptr<ShutdownManager> shutdown_manager);

  // [fuchsia::ui::lifecycle::LifecycleController]
  void Terminate() override;

 private:
  fidl::BindingSet<fuchsia::ui::lifecycle::LifecycleController> bindings_;
  std::weak_ptr<ShutdownManager> shutdown_manager_;
};

}  // namespace scenic_impl

#endif  // SRC_UI_SCENIC_LIB_SHUTDOWN_LIFECYCLE_CONTROLLER_IMPL_H_
