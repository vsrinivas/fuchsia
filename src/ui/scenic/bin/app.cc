// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/scenic/bin/app.h"

#include <lib/fit/bridge.h>
#include <lib/fit/function.h>
#include <lib/fit/single_threaded_executor.h>
#include <lib/syslog/cpp/macros.h>

#ifdef SCENIC_ENABLE_GFX_SUBSYSTEM
#include "src/ui/scenic/lib/gfx/gfx_system.h"
#endif

#ifdef SCENIC_ENABLE_INPUT_SUBSYSTEM
#include "src/ui/scenic/lib/input/input_system.h"
#endif

#include "src/lib/cobalt/cpp/cobalt_logger.h"
#include "src/lib/files/file.h"
#include "src/ui/lib/escher/vk/pipeline_builder.h"
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

zx::duration GetMinimumPredictedFrameDuration() {
  std::string frame_scheduler_min_predicted_frame_duration;
  int frame_scheduler_min_predicted_frame_duration_in_us = 0;
  if (files::ReadFileToString("/config/data/frame_scheduler_min_predicted_frame_duration_in_us",
                              &frame_scheduler_min_predicted_frame_duration)) {
    frame_scheduler_min_predicted_frame_duration_in_us =
        atoi(frame_scheduler_min_predicted_frame_duration.c_str());
    FX_DCHECK(frame_scheduler_min_predicted_frame_duration_in_us >= 0);
    FX_LOGS(INFO) << "min_predicted_frame_duration(us): "
                  << frame_scheduler_min_predicted_frame_duration_in_us;
  }
  return frame_scheduler_min_predicted_frame_duration_in_us > 0
             ? zx::usec(frame_scheduler_min_predicted_frame_duration_in_us)
             : scheduling::DefaultFrameScheduler::kMinPredictedFrameDuration;
}

}  // namespace

namespace scenic_impl {

DisplayInfoDelegate::DisplayInfoDelegate(std::shared_ptr<display::Display> display_)
    : display_(display_) {
  FX_CHECK(display_);
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
    FX_LOGS(ERROR) << "Display ownership event duplication error.";
    callback(zx::event());
  } else {
    callback(std::move(dup));
  }
}

App::App(std::unique_ptr<sys::ComponentContext> app_context, inspect::Node inspect_node,
         fit::closure quit_callback)
    : executor_(async_get_default_dispatcher()),
      app_context_(std::move(app_context)),
      // TODO(fxbug.dev/40997): subsystems requiring graceful shutdown *on a loop* should register
      // themselves. It is preferable to cleanly shutdown using destructors only, if possible.
      shutdown_manager_(
          ShutdownManager::New(async_get_default_dispatcher(), std::move(quit_callback))),
      scenic_(std::make_shared<Scenic>(app_context_.get(), std::move(inspect_node),
                                       [weak = std::weak_ptr<ShutdownManager>(shutdown_manager_)] {
                                         if (auto strong = weak.lock()) {
                                           strong->Shutdown(
                                               LifecycleControllerImpl::kShutdownTimeout);
                                         }
                                       })),
      annotation_registry_(app_context_.get()),
      lifecycle_controller_impl_(app_context_.get(),
                                 std::weak_ptr<ShutdownManager>(shutdown_manager_)) {
  FX_DCHECK(!device_watcher_);

  fit::bridge<escher::EscherUniquePtr> escher_bridge;
  fit::bridge<std::shared_ptr<scenic_impl::display::Display>> display_bridge;

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
          .and_then([this](std::tuple<fit::result<escher::EscherUniquePtr>,
                                      fit::result<std::shared_ptr<scenic_impl::display::Display>>>&
                               results) {
            InitializeServices(std::move(std::get<0>(results).value()),
                               std::move(std::get<1>(results).value()));
          });

  executor_.schedule_task(std::move(p));

  // TODO(fxbug.dev/48596): Scenic sometimes gets stuck for consecutive 60 seconds.
  // Here we set up a Watchdog polling Scenic status every 15 seconds.
  constexpr uint32_t kWatchdogWarningIntervalMs = 15000u;

  // On some devices, the time to start up Scenic may exceed 15 seconds.
  // In that case we should only send a warning, and we should only crash
  // Scenic if the main thread is blocked for longer time.
  constexpr uint32_t kWatchdogTimeoutMs = 45000u;

  watchdog_ = std::make_unique<Watchdog>(kWatchdogWarningIntervalMs, kWatchdogTimeoutMs,
                                         async_get_default_dispatcher());
}

void App::InitializeServices(escher::EscherUniquePtr escher,
                             std::shared_ptr<display::Display> display) {
  TRACE_DURATION("gfx", "App::InitializeServices");

  if (!display) {
    FX_LOGS(ERROR) << "No default display, Graphics system exiting";
    shutdown_manager_->Shutdown(LifecycleControllerImpl::kShutdownTimeout);
    return;
  }

  if (!escher || !escher->device()) {
    FX_LOGS(ERROR) << "No Vulkan on device, Graphics system exiting.";
    shutdown_manager_->Shutdown(LifecycleControllerImpl::kShutdownTimeout);
    return;
  }

  escher_ = std::move(escher);

  std::shared_ptr<cobalt::CobaltLogger> cobalt_logger = cobalt::NewCobaltLoggerFromProjectId(
      async_get_default_dispatcher(), app_context_->svc(), cobalt_registry::kProjectId);
  if (!cobalt_logger) {
    FX_LOGS(ERROR) << "CobaltLogger creation failed!";
  }

  // Replace Escher's default pipeline builder with one which will log to Cobalt upon each
  // unexpected lazy pipeline creation.  This allows us to detect when this slips through our
  // testing and occurs in the wild.  In order to detect problems ASAP during development, debug
  // builds CHECK instead of logging to Cobalt.
  {
    auto pipeline_builder = std::make_unique<escher::PipelineBuilder>(escher_->vk_device());
    pipeline_builder->set_log_pipeline_creation_callback(
        [cobalt_logger](const vk::GraphicsPipelineCreateInfo* graphics_info,
                        const vk::ComputePipelineCreateInfo* compute_info) {
          // TODO(fxbug.dev/49972): pre-warm compute pipelines in addition to graphics pipelines.
          if (compute_info) {
            FX_LOGS(WARNING) << "Unexpected lazy creation of Vulkan compute pipeline.";
            return;
          }

#if !defined(NDEBUG)
          FX_CHECK(false)  // debug builds should crash for early detection
#else
          FX_LOGS(WARNING)  // release builds should log to Cobalt, see below.
#endif
              << "Unexpected lazy creation of Vulkan pipeline.";

          cobalt_logger->LogEvent(
              cobalt_registry::kScenicRareEventMetricId,
              cobalt_registry::ScenicRareEventMetricDimensionEvent_LazyPipelineCreation);
        });
    escher_->set_pipeline_builder(std::move(pipeline_builder));
  }

  {
    TRACE_DURATION("gfx", "App::InitializeServices[frame-scheduler]");
    frame_scheduler_ = std::make_shared<scheduling::DefaultFrameScheduler>(
        display->vsync_timing(),
        std::make_unique<scheduling::WindowedFramePredictor>(
            GetMinimumPredictedFrameDuration(),
            scheduling::DefaultFrameScheduler::kInitialRenderDuration,
            scheduling::DefaultFrameScheduler::kInitialUpdateDuration),
        scenic_->inspect_node()->CreateChild("FrameScheduler"), cobalt_logger);
  }

  {
    TRACE_DURATION("gfx", "App::InitializeServices[engine]");
    engine_ =
        std::make_shared<gfx::Engine>(app_context_.get(), frame_scheduler_, escher_->GetWeakPtr(),
                                      scenic_->inspect_node()->CreateChild("Engine"));
  }
  frame_scheduler_->SetFrameRenderer(engine_);
  scenic_->SetFrameScheduler(frame_scheduler_);
  annotation_registry_.InitializeWithGfxAnnotationManager(engine_->annotation_manager());

#ifdef SCENIC_ENABLE_GFX_SUBSYSTEM
  auto gfx = scenic_->RegisterSystem<gfx::GfxSystem>(engine_.get(), &sysmem_, &display_manager_);
  FX_DCHECK(gfx);

  frame_scheduler_->AddSessionUpdater(scenic_);
  scenic_->SetScreenshotDelegate(gfx.get());
  display_info_delegate_ = std::make_unique<DisplayInfoDelegate>(display);
  scenic_->SetDisplayInfoDelegate(display_info_delegate_.get());
#endif

#ifdef SCENIC_ENABLE_INPUT_SUBSYSTEM
  auto input = scenic_->RegisterSystem<input::InputSystem>(engine_->scene_graph());
  FX_DCHECK(input);
#endif

  // Create the snapshotter and pass it to scenic.
  auto snapshotter =
      std::make_unique<gfx::InternalSnapshotImpl>(engine_->scene_graph(), escher_->GetWeakPtr());
  scenic_->InitializeSnapshotService(std::move(snapshotter));

  scenic_->SetInitialized(engine_->scene_graph());
}

}  // namespace scenic_impl
