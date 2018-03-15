// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_LIB_UI_GFX_SCENIC_SYSTEM_H_
#define GARNET_LIB_UI_GFX_SCENIC_SYSTEM_H_

#include "garnet/lib/ui/gfx/displays/display_manager.h"
#include "garnet/lib/ui/gfx/engine/engine.h"
#include "garnet/lib/ui/scenic/system.h"
#include "lib/escher/escher.h"

namespace scenic {
namespace gfx {

class ScenicSystem : public TempSystemDelegate {
 public:
  static constexpr TypeId kTypeId = kScenic;

  explicit ScenicSystem(SystemContext context);
  ~ScenicSystem();

  std::unique_ptr<CommandDispatcher> CreateCommandDispatcher(
      CommandDispatcherContext context) override;

  // TODO(MZ-452): Remove this when we externalize Displays.
  void GetDisplayInfo(
      const ui::Scenic::GetDisplayInfoCallback& callback) override;

 private:
  void Initialize();

  DisplayManager display_manager_;

  // TODO(MZ-452): Remove this when we externalize Displays.
  void GetDisplayInfoImmediately(
      const ui::Scenic::GetDisplayInfoCallback& callback);

  // TODO(MZ-452): Remove this when we externalize Displays.
  bool initialized_ = false;
  std::vector<fxl::Closure> run_after_initialized_;

  escher::VulkanInstancePtr vulkan_instance_;
  escher::VulkanDeviceQueuesPtr vulkan_device_queues_;
  vk::SurfaceKHR surface_;
  std::unique_ptr<escher::Escher> escher_;
  std::unique_ptr<Engine> engine_;
};

}  // namespace gfx
}  // namespace scenic

#endif  // GARNET_LIB_UI_GFX_SCENIC_SYSTEM_H_
