// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_LIB_UI_GFX_GFX_SYSTEM_H_
#define GARNET_LIB_UI_GFX_GFX_SYSTEM_H_

#include "garnet/lib/ui/gfx/displays/display_manager.h"
#include "garnet/lib/ui/gfx/engine/engine.h"
#include "garnet/lib/ui/scenic/system.h"
#include "lib/escher/escher.h"

namespace scenic {
namespace gfx {

class GfxSystem : public TempSystemDelegate {
 public:
  static constexpr TypeId kTypeId = kGfx;

  explicit GfxSystem(SystemContext context);
  ~GfxSystem();

  std::unique_ptr<CommandDispatcher> CreateCommandDispatcher(
      CommandDispatcherContext context) override;

  // TODO(MZ-452): Remove this when we externalize Displays.
  void GetDisplayInfo(
      fuchsia::ui::scenic::Scenic::GetDisplayInfoCallback callback) override;
  void TakeScreenshot(
      fuchsia::ui::scenic::Scenic::TakeScreenshotCallback callback) override;
  void GetDisplayOwnershipEvent(
      fuchsia::ui::scenic::Scenic::GetDisplayOwnershipEventCallback callback)
      override;

 protected:
  // Protected so test classes can expose.
  std::unique_ptr<Engine> engine_;
  virtual std::unique_ptr<escher::Escher> InitializeEscher();
  virtual std::unique_ptr<Engine> InitializeEngine();

 private:
  void Initialize();

  DisplayManager display_manager_;

  // TODO(MZ-452): Remove this when we externalize Displays.
  void GetDisplayInfoImmediately(
      fuchsia::ui::scenic::Scenic::GetDisplayInfoCallback callback);
  void GetDisplayOwnershipEventImmediately(
      fuchsia::ui::scenic::Scenic::GetDisplayOwnershipEventCallback callback);

  // Redirect to instance method.
  static VkBool32 RedirectDebugReport(VkDebugReportFlagsEXT flags,
                                      VkDebugReportObjectTypeEXT objectType,
                                      uint64_t object, size_t location,
                                      int32_t messageCode,
                                      const char* pLayerPrefix,
                                      const char* pMessage, void* pUserData) {
    return reinterpret_cast<GfxSystem*>(pUserData)->HandleDebugReport(
        flags, objectType, object, location, messageCode, pLayerPrefix,
        pMessage);
  }

  VkBool32 HandleDebugReport(VkDebugReportFlagsEXT flags,
                             VkDebugReportObjectTypeEXT objectType,
                             uint64_t object, size_t location,
                             int32_t messageCode, const char* pLayerPrefix,
                             const char* pMessage);

  // TODO(MZ-452): Remove this when we externalize Displays.
  bool initialized_ = false;
  std::vector<fit::closure> run_after_initialized_;

  escher::VulkanInstancePtr vulkan_instance_;
  escher::VulkanDeviceQueuesPtr vulkan_device_queues_;
  vk::SurfaceKHR surface_;
  std::unique_ptr<escher::Escher> escher_;

  VkDebugReportCallbackEXT debug_report_callback_;
};

}  // namespace gfx
}  // namespace scenic

#endif  // GARNET_LIB_UI_GFX_GFX_SYSTEM_H_
