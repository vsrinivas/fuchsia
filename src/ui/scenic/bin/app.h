// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_SCENIC_BIN_APP_H_
#define SRC_UI_SCENIC_BIN_APP_H_

#include <lib/async/cpp/executor.h>

#include <memory>
#include <optional>

#include "lib/async/dispatcher.h"
#include "src/lib/async-watchdog/watchdog.h"
#include "src/lib/fsl/io/device_watcher.h"
#include "src/ui/lib/display/get_hardware_display_controller.h"
#include "src/ui/lib/escher/escher.h"
#include "src/ui/scenic/bin/temporary_frame_renderer_delegator.h"
#include "src/ui/scenic/lib/allocation/allocator.h"
#include "src/ui/scenic/lib/annotation/annotation_registry.h"
#include "src/ui/scenic/lib/display/display_manager.h"
#include "src/ui/scenic/lib/flatland/default_flatland_presenter.h"
#include "src/ui/scenic/lib/flatland/engine/display_compositor.h"
#include "src/ui/scenic/lib/flatland/engine/engine.h"
#include "src/ui/scenic/lib/flatland/engine/engine_types.h"
#include "src/ui/scenic/lib/flatland/flatland_manager.h"
#include "src/ui/scenic/lib/flatland/link_system.h"
#include "src/ui/scenic/lib/flatland/uber_struct_system.h"
#include "src/ui/scenic/lib/focus/focus_manager.h"
#include "src/ui/scenic/lib/gfx/engine/engine.h"
#include "src/ui/scenic/lib/input/input_system.h"
#include "src/ui/scenic/lib/scenic/scenic.h"
#include "src/ui/scenic/lib/scheduling/default_frame_scheduler.h"
#include "src/ui/scenic/lib/screenshot/screenshot_manager.h"
#include "src/ui/scenic/lib/shutdown/lifecycle_controller_impl.h"
#include "src/ui/scenic/lib/shutdown/shutdown_manager.h"
#include "src/ui/scenic/lib/view_tree/view_ref_installed_impl.h"
#include "src/ui/scenic/lib/view_tree/view_tree_snapshotter.h"

namespace scenic_impl {

class DisplayInfoDelegate : public Scenic::GetDisplayInfoDelegateDeprecated {
 public:
  DisplayInfoDelegate(std::shared_ptr<display::Display> display);

  // TODO(fxbug.dev/23686): Remove this when we externalize Displays.
  // |Scenic::GetDisplayInfoDelegateDeprecated|
  void GetDisplayInfo(fuchsia::ui::scenic::Scenic::GetDisplayInfoCallback callback) override;
  // |Scenic::GetDisplayInfoDelegateDeprecated|
  void GetDisplayOwnershipEvent(
      fuchsia::ui::scenic::Scenic::GetDisplayOwnershipEventCallback callback) override;

 private:
  std::shared_ptr<display::Display> display_ = nullptr;
};

// Values read from config file. Set to their default values.
struct ConfigValues {
  zx::duration min_predicted_frame_duration =
      scheduling::DefaultFrameScheduler::kMinPredictedFrameDuration;
  bool enable_allocator_for_flatland = true;
  bool pointer_auto_focus_on = true;
  flatland::BufferCollectionImportMode flatland_buffer_collection_import_mode =
      flatland::BufferCollectionImportMode::AttemptDisplayConstraints;
  // TODO(fxb/76985): Remove these when we have proper multi-display support.
  std::optional<uint64_t> i_can_haz_display_id;
  std::optional<size_t> i_can_haz_display_mode;
};

class App {
 public:
  explicit App(std::unique_ptr<sys::ComponentContext> app_context, inspect::Node inspect_node,
               fpromise::promise<ui_display::DisplayControllerHandles> dc_handles_promise,
               fit::closure quit_callback);

  ~App();

 private:
  void InitializeServices(escher::EscherUniquePtr escher,
                          std::shared_ptr<display::Display> display);

  void CreateFrameScheduler(std::shared_ptr<const scheduling::VsyncTiming> vsync_timing);
  void InitializeGraphics(std::shared_ptr<display::Display> display);
  void InitializeInput();
  void InitializeHeartbeat();

  void DoPeriodicLogging(async_dispatcher_t*, async::TaskBase*, zx_status_t);

  async::Executor executor_;
  std::unique_ptr<sys::ComponentContext> app_context_;
  const ConfigValues config_values_;

  std::shared_ptr<ShutdownManager> shutdown_manager_;
  std::shared_ptr<cobalt::CobaltLogger> cobalt_logger_;

  gfx::Sysmem sysmem_;
  std::unique_ptr<display::DisplayManager> display_manager_;
  std::unique_ptr<DisplayInfoDelegate> display_info_delegate_;
  escher::EscherUniquePtr escher_;

  std::shared_ptr<gfx::ImagePipeUpdater> image_pipe_updater_;
  std::shared_ptr<gfx::Engine> engine_;
  std::shared_ptr<Scenic> scenic_;
  std::unique_ptr<fsl::DeviceWatcher> device_watcher_;
  std::unique_ptr<async_watchdog::Watchdog> watchdog_;

  std::shared_ptr<allocation::Allocator> allocator_;

  std::shared_ptr<flatland::UberStructSystem> uber_struct_system_;
  std::shared_ptr<flatland::LinkSystem> link_system_;
  std::shared_ptr<flatland::DefaultFlatlandPresenter> flatland_presenter_;
  std::shared_ptr<flatland::FlatlandManager> flatland_manager_;
  std::shared_ptr<flatland::DisplayCompositor> flatland_compositor_;
  std::shared_ptr<flatland::Engine> flatland_engine_;

  std::shared_ptr<TemporaryFrameRendererDelegator> frame_renderer_;

  std::shared_ptr<input::InputSystem> input_;
  std::unique_ptr<focus::FocusManager> focus_manager_;

  std::shared_ptr<scheduling::DefaultFrameScheduler> frame_scheduler_;

  std::shared_ptr<view_tree::ViewTreeSnapshotter> view_tree_snapshotter_;

  std::unique_ptr<screenshot::ScreenshotManager> screenshot_manager_;

  view_tree::ViewRefInstalledImpl view_ref_installed_impl_;

  AnnotationRegistry annotation_registry_;

  LifecycleControllerImpl lifecycle_controller_impl_;

  async::TaskMethod<App, &App::DoPeriodicLogging> periodic_logging_task_{this};

  const bool enable_snapshot_dump_ = false;
};

}  // namespace scenic_impl

#endif  // SRC_UI_SCENIC_BIN_APP_H_
