// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/scenic/bin/app.h"

#include <lib/fit/bridge.h>
#include <lib/fit/function.h>
#include <lib/fit/single_threaded_executor.h>
#include <lib/syslog/cpp/logger.h>

#ifdef SCENIC_ENABLE_GFX_SUBSYSTEM
#include "src/ui/scenic/lib/gfx/gfx_system.h"
#endif

#ifdef SCENIC_ENABLE_INPUT_SUBSYSTEM
#include "src/ui/scenic/lib/input/input_system.h"
#endif

#include "src/lib/cobalt/cpp/cobalt_logger.h"
#include "src/ui/scenic/lib/gfx/api/internal_snapshot_impl.h"
#include "src/ui/scenic/lib/scheduling/default_frame_scheduler.h"
#include "src/ui/scenic/lib/scheduling/frame_metrics_registry.cb.h"
#include "src/ui/scenic/lib/scheduling/windowed_frame_predictor.h"

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

DisplayInfoDelegate::DisplayInfoDelegate(std::shared_ptr<gfx::Display> display_)
    : display_(display_) {
  FXL_CHECK(display_);
}

void DisplayInfoDelegate::GetDisplayInfo(
    fuchsia::ui::scenic::Scenic::GetDisplayInfoCallback callback) {
  auto info = ::fuchsia::ui::gfx::DisplayInfo();
  info.width_in_px = display_->width_in_px();
  info.height_in_px = display_->height_in_px();

  callback(std::move(info));
}

void DisplayInfoDelegate::GetDisplayOwnershipEvent(
    fuchsia::ui::scenic::Scenic::GetDisplayOwnershipEventCallback callback) {
  // These constants are defined as raw hex in the FIDL file, so we confirm here that they are the
  // same values as the expected constants in the ZX headers.
  static_assert(fuchsia::ui::scenic::displayNotOwnedSignal == ZX_USER_SIGNAL_0, "Bad constant");
  static_assert(fuchsia::ui::scenic::displayOwnedSignal == ZX_USER_SIGNAL_1, "Bad constant");

  zx::event dup;
  if (display_->ownership_event().duplicate(ZX_RIGHTS_BASIC, &dup) != ZX_OK) {
    FXL_LOG(ERROR) << "Display ownership event duplication error.";
    callback(zx::event());
  } else {
    callback(std::move(dup));
  }
}

App::App(std::unique_ptr<sys::ComponentContext> app_context, inspect_deprecated::Node inspect_node,
         fit::closure quit_callback)
    : executor_(async_get_default_dispatcher()),
      app_context_(std::move(app_context)),
      // TODO(40997): subsystems requiring graceful shutdown *on a loop* should register themselves.
      // It is preferable to cleanly shutdown using destructors only, if possible.
      shutdown_manager_(async_get_default_dispatcher(), std::move(quit_callback)),
      scenic_(app_context_.get(), std::move(inspect_node),
              [weak = shutdown_manager_.GetWeakPtr()] {
                if (weak) {
                  weak->Shutdown(LifecycleControllerImpl::kShutdownTimeout);
                };
              }),
      lifecycle_controller_impl_(app_context_.get(), shutdown_manager_.GetWeakPtr()) {
  FXL_DCHECK(!device_watcher_);

  fit::bridge<escher::EscherUniquePtr> escher_bridge;
  fit::bridge<std::shared_ptr<scenic_impl::gfx::Display>> display_bridge;

  device_watcher_ = fsl::DeviceWatcher::Create(
      kDependencyDir, [this, completer = std::move(escher_bridge.completer)](
                          int dir_fd, std::string filename) mutable {
        completer.complete_ok(gfx::GfxSystem::CreateEscher(app_context_.get()));
        device_watcher_.reset();
      });

  display_manager_.WaitForDefaultDisplayController(
      [this, completer = std::move(display_bridge.completer)]() mutable {
        completer.complete_ok(display_manager_.default_display_shared());
      });

  auto p =
      fit::join_promises(escher_bridge.consumer.promise(), display_bridge.consumer.promise())
          .and_then(
              [this](std::tuple<fit::result<escher::EscherUniquePtr>,
                                fit::result<std::shared_ptr<scenic_impl::gfx::Display>>>& results) {
                InitializeServices(std::move(std::get<0>(results).value()),
                                   std::move(std::get<1>(results).value()));
              });

  executor_.schedule_task(std::move(p));
}

void App::InitializeServices(escher::EscherUniquePtr escher,
                             std::shared_ptr<gfx::Display> display) {
  if (!display) {
    FXL_LOG(ERROR) << "No default display, Graphics system exiting";
    shutdown_manager_.Shutdown(LifecycleControllerImpl::kShutdownTimeout);
    return;
  }

  if (!escher || !escher->device()) {
    FXL_LOG(ERROR) << "No Vulkan on device, Graphics system exiting.";
    shutdown_manager_.Shutdown(LifecycleControllerImpl::kShutdownTimeout);
    return;
  }

  escher_ = std::move(escher);

  std::unique_ptr<cobalt::CobaltLogger> cobalt_logger = cobalt::NewCobaltLoggerFromProjectName(
      async_get_default_dispatcher(), app_context_->svc(), cobalt_registry::kProjectName);
  if (cobalt_logger == nullptr) {
    FX_LOGS(ERROR) << "CobaltLogger creation failed!";
  }
  frame_scheduler_ = std::make_shared<scheduling::DefaultFrameScheduler>(
      display,
      std::make_unique<scheduling::WindowedFramePredictor>(
          scheduling::DefaultFrameScheduler::kInitialRenderDuration,
          scheduling::DefaultFrameScheduler::kInitialUpdateDuration),
      scenic_.inspect_node()->CreateChild("FrameScheduler"), std::move(cobalt_logger));

  engine_.emplace(app_context_.get(), frame_scheduler_, escher_->GetWeakPtr(),
                  scenic_.inspect_node()->CreateChild("Engine"));
  frame_scheduler_->SetFrameRenderer(engine_->GetWeakPtr());

#ifdef SCENIC_ENABLE_GFX_SUBSYSTEM
  auto gfx = scenic_.RegisterSystem<gfx::GfxSystem>(&engine_.value(), escher_->GetWeakPtr(),
                                                    &sysmem_, &display_manager_);

  FXL_DCHECK(gfx);

  frame_scheduler_->AddSessionUpdater(gfx->GetWeakPtr());
  scenic_.SetScreenshotDelegate(gfx);
  display_info_delegate_ = std::make_unique<DisplayInfoDelegate>(display);
  scenic_.SetDisplayInfoDelegate(display_info_delegate_.get());
#endif

#ifdef SCENIC_ENABLE_INPUT_SUBSYSTEM
  auto input = scenic_.RegisterSystem<input::InputSystem>(&engine_.value());
  FXL_DCHECK(input);
#endif

  // Create the snapshotter and pass it to scenic.
  auto snapshotter =
      std::make_unique<gfx::InternalSnapshotImpl>(engine_->scene_graph(), escher_->GetWeakPtr());
  scenic_.InitializeSnapshotService(std::move(snapshotter));

  scenic_.SetInitialized();
}

}  // namespace scenic_impl
