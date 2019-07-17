// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/ui/scenic/app.h"

#ifdef SCENIC_ENABLE_GFX_SUBSYSTEM
#include "garnet/lib/ui/gfx/gfx_system.h"
#endif

#ifdef SCENIC_ENABLE_INPUT_SUBSYSTEM
#include "garnet/lib/ui/input/input_system.h"
#endif

namespace {

// Wait for /dev/class/display-controller on x86 as that's sufficient for
// Intel GPU driver and supports AEMU and swiftshader, which don't depend
// on devices in /dev/class/gpu.
//
// TODO(SCN-568): Scenic should not be aware of these type of dependencies.
#if defined(__x86_64__)
static const std::string kDependencyDir = "/dev/class/display-controller";
#else
static const std::string kDependencyDir = "/dev/class/gpu";
#endif

// A limited System used only to limit Scenic from fully initializing, without introducing a new
// command dispatcher.
//
// TODO(SCN-1506): Find a better way to represent this other than creating an entire dummy system.
class Dependency : public scenic_impl::System {
 public:
  using System::System;
  scenic_impl::CommandDispatcherUniquePtr CreateCommandDispatcher(
      scenic_impl::CommandDispatcherContext context) override {
    return nullptr;
  };
};
}  // namespace

namespace scenic_impl {

App::App(sys::ComponentContext* app_context, inspect_deprecated::Node inspect_node,
         fit::closure quit_callback)
    : scenic_(std::make_unique<Scenic>(app_context, std::move(inspect_node),
                                       std::move(quit_callback))) {
  FXL_DCHECK(!device_watcher_);

  std::unique_ptr<System> dependency = std::make_unique<Dependency>(
      SystemContext(scenic_->app_context(), inspect_deprecated::Node(), /*quit_callback*/ nullptr),
      false);
  scenic_->RegisterDependency(dependency.get());

  device_watcher_ = fsl::DeviceWatcher::Create(
      kDependencyDir,
      [this, dependency = std::move(dependency)](int dir_fd, std::string filename) {
        escher_ = gfx::GfxSystem::CreateEscher(scenic_->app_context());

#ifdef SCENIC_ENABLE_GFX_SUBSYSTEM
        auto gfx = scenic_->RegisterSystem<gfx::GfxSystem>(std::make_unique<gfx::DisplayManager>(),
                                                           escher_->GetWeakPtr());
        scenic_->SetDelegate(gfx);
        FXL_DCHECK(gfx);
#endif

#ifdef SCENIC_ENABLE_INPUT_SUBSYSTEM
#ifdef SCENIC_ENABLE_GFX_SUBSYSTEM
        auto input = scenic_->RegisterSystem<input::InputSystem>(gfx);
        FXL_DCHECK(input);
#else
#error InputSystem requires gfx::GfxSystem.
#endif
#endif

        dependency->SetToInitialized();
      });
}

}  // namespace scenic_impl
