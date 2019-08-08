// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/ui/scenic/app.h"

#include <lib/fit/bridge.h>
#include <lib/fit/function.h>
#include <lib/fit/single_threaded_executor.h>

#ifdef SCENIC_ENABLE_GFX_SUBSYSTEM
#include "garnet/lib/ui/gfx/gfx_system.h"
#endif

#ifdef SCENIC_ENABLE_INPUT_SUBSYSTEM
#include "garnet/lib/ui/input/input_system.h"
#endif

#include "garnet/lib/ui/gfx/engine/default_frame_scheduler.h"
#include "garnet/lib/ui/gfx/engine/frame_predictor.h"

namespace {

// Wait for /dev/class/display-controller on x86 as that's sufficient for Intel GPU driver and
// supports AEMU and swiftshader, which don't depend on devices in /dev/class/gpu.
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
    : executor_(async_get_default_dispatcher()),
      scenic_(app_context, std::move(inspect_node), std::move(quit_callback)) {
  FXL_DCHECK(!device_watcher_);

  fit::bridge<escher::EscherUniquePtr> escher_bridge;
  fit::bridge<scenic_impl::gfx::Display*> display_bridge;

  device_watcher_ = fsl::DeviceWatcher::Create(
      kDependencyDir, [this, completer = std::move(escher_bridge.completer)](
                          int dir_fd, std::string filename) mutable {
        completer.complete_ok(gfx::GfxSystem::CreateEscher(scenic_.app_context()));
        device_watcher_.reset();
      });

  display_manager_.WaitForDefaultDisplayController(
      [this, completer = std::move(display_bridge.completer)]() mutable {
        completer.complete_ok(display_manager_.default_display());
      });

  auto p = fit::join_promises(escher_bridge.consumer.promise(), display_bridge.consumer.promise())
               .and_then([this](std::tuple<fit::result<escher::EscherUniquePtr>,
                                           fit::result<scenic_impl::gfx::Display*>>& results) {
                 InitializeServices(std::move(std::get<0>(results).value()),
                                    std::move(std::get<1>(results).value()));
               });

  executor_.schedule_task(std::move(p));
}

void App::InitializeServices(escher::EscherUniquePtr escher, gfx::Display* display) {
  if (!display) {
    FXL_LOG(ERROR) << "No default display, Graphics system exiting";
    scenic_.Quit();
    return;
  }

  if (!escher || !escher->device()) {
    FXL_LOG(ERROR) << "No Vulkan on device, Graphics system exiting.";
    scenic_.Quit();
    return;
  }

  escher_ = std::move(escher);

  frame_scheduler_ = std::make_shared<gfx::DefaultFrameScheduler>(
      display,
      std::make_unique<gfx::FramePredictor>(gfx::DefaultFrameScheduler::kInitialRenderDuration,
                                            gfx::DefaultFrameScheduler::kInitialUpdateDuration),
      scenic_.inspect_node()->CreateChild("FrameScheduler"));

  engine_.emplace(frame_scheduler_, &display_manager_, escher_->GetWeakPtr(),
                  scenic_.inspect_node()->CreateChild("Engine"));
  frame_scheduler_->SetFrameRenderer(engine_->GetWeakPtr());

#ifdef SCENIC_ENABLE_GFX_SUBSYSTEM
  auto gfx =
      scenic_.RegisterSystem<gfx::GfxSystem>(display, &engine_.value(), escher_->GetWeakPtr());
  frame_scheduler_->AddSessionUpdater(gfx->GetWeakPtr());
  scenic_.SetDelegate(gfx);
  FXL_DCHECK(gfx);
#endif

#ifdef SCENIC_ENABLE_INPUT_SUBSYSTEM
  auto input = scenic_.RegisterSystem<input::InputSystem>(&engine_.value());
  FXL_DCHECK(input);
#endif

  scenic_.SetInitialized();
}

}  // namespace scenic_impl
