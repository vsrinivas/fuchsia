// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_SCENIC_BIN_APP_H_
#define SRC_UI_SCENIC_BIN_APP_H_

#include <lib/async/cpp/executor.h>
#include <lib/fit/function.h>

#include <memory>

#include "src/lib/fsl/io/device_watcher.h"
#include "src/ui/lib/escher/escher.h"
#include "src/ui/scenic/lib/display/display_manager.h"
#include "src/ui/scenic/lib/gfx/engine/engine.h"
#include "src/ui/scenic/lib/scenic/scenic.h"
#include "src/ui/scenic/lib/scheduling/frame_scheduler.h"
#include "src/ui/scenic/lib/shutdown/lifecycle_controller_impl.h"
#include "src/ui/scenic/lib/shutdown/shutdown_manager.h"

namespace scenic_impl {

class DisplayInfoDelegate : public Scenic::GetDisplayInfoDelegateDeprecated {
 public:
  DisplayInfoDelegate(std::shared_ptr<display::Display> display);

  // TODO(fxb/23686): Remove this when we externalize Displays.
  // |Scenic::GetDisplayInfoDelegateDeprecated|
  void GetDisplayInfo(fuchsia::ui::scenic::Scenic::GetDisplayInfoCallback callback) override;
  // |Scenic::GetDisplayInfoDelegateDeprecated|
  void GetDisplayOwnershipEvent(
      fuchsia::ui::scenic::Scenic::GetDisplayOwnershipEventCallback callback) override;

 private:
  std::shared_ptr<display::Display> display_ = nullptr;
};

class App {
 public:
  explicit App(std::unique_ptr<sys::ComponentContext> app_context,
               inspect_deprecated::Node inspect_node, fit::closure quit_callback);

 private:
  void InitializeServices(escher::EscherUniquePtr escher,
                          std::shared_ptr<display::Display> display);

  async::Executor executor_;
  std::unique_ptr<sys::ComponentContext> app_context_;
  std::shared_ptr<ShutdownManager> shutdown_manager_;

  gfx::Sysmem sysmem_;
  display::DisplayManager display_manager_;
  std::unique_ptr<DisplayInfoDelegate> display_info_delegate_;
  escher::EscherUniquePtr escher_;
  std::shared_ptr<scheduling::FrameScheduler> frame_scheduler_;

  std::optional<gfx::Engine> engine_;
  Scenic scenic_;
  std::unique_ptr<fsl::DeviceWatcher> device_watcher_;

  LifecycleControllerImpl lifecycle_controller_impl_;
};

}  // namespace scenic_impl

#endif  // SRC_UI_SCENIC_BIN_APP_H_
