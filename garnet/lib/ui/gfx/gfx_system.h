// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_LIB_UI_GFX_GFX_SYSTEM_H_
#define GARNET_LIB_UI_GFX_GFX_SYSTEM_H_

#include <memory>

#include "garnet/lib/ui/gfx/displays/display_manager.h"
#include "garnet/lib/ui/gfx/engine/engine.h"
#include "garnet/lib/ui/gfx/engine/gfx_command_applier.h"
#include "garnet/lib/ui/gfx/resources/compositor/compositor.h"
#include "garnet/lib/ui/scenic/system.h"
#include "src/ui/lib/escher/escher.h"

namespace scenic_impl {
namespace gfx {

class Compositor;

class GfxSystem : public TempSystemDelegate, public SessionUpdater {
 public:
  static constexpr TypeId kTypeId = kGfx;
  static const char* kName;

  GfxSystem(SystemContext context, std::unique_ptr<DisplayManager> display_manager);
  ~GfxSystem();

  CommandDispatcherUniquePtr CreateCommandDispatcher(CommandDispatcherContext context) override;

  // TODO(SCN-452): Remove this when we externalize Displays.
  void GetDisplayInfo(fuchsia::ui::scenic::Scenic::GetDisplayInfoCallback callback) override;
  void TakeScreenshot(fuchsia::ui::scenic::Scenic::TakeScreenshotCallback callback) override;
  void GetDisplayOwnershipEvent(
      fuchsia::ui::scenic::Scenic::GetDisplayOwnershipEventCallback callback) override;

  // |SessionUpdater|
  virtual UpdateResults UpdateSessions(std::unordered_set<SessionId> sessions_to_update,
                                       zx_time_t presentation_time, uint64_t trace_id) override;

  // |SessionUpdater|
  virtual void PrepareFrame(zx_time_t presentation_time, uint64_t trace_id) override;

  // TODO(SCN-906): Break out Engine, instead of coupling it to GfxSystem.
  CompositorWeakPtr GetCompositor(GlobalId compositor_id) const;
  gfx::Session* GetSession(SessionId session_id) const;

  // TODO(SCN-906): Remove this in favor of unified initialization.
  void AddInitClosure(fit::closure closure);

  // For tests.
  SessionManager* session_manager() { return session_manager_.get(); }

 protected:
  // Protected so test classes can expose.
  //
  // TODO(SCN-1491): Replace with dependency injection.
  virtual std::unique_ptr<SessionManager> InitializeSessionManager();
  virtual std::unique_ptr<escher::Escher> InitializeEscher();
  virtual std::unique_ptr<Engine> InitializeEngine();

  std::shared_ptr<FrameScheduler> frame_scheduler_;
  std::unique_ptr<SessionManager> session_manager_;
  std::unique_ptr<Engine> engine_;
  std::unique_ptr<DisplayManager> display_manager_;
  std::unique_ptr<escher::Escher> escher_;

 private:
  fit::closure DelayedInitClosure();
  void Initialize();

  // TODO(SCN-452): Remove this when we externalize Displays.
  void GetDisplayInfoImmediately(fuchsia::ui::scenic::Scenic::GetDisplayInfoCallback callback);
  void GetDisplayOwnershipEventImmediately(
      fuchsia::ui::scenic::Scenic::GetDisplayOwnershipEventCallback callback);

  // Redirect to instance method.
  static VkBool32 RedirectDebugReport(VkDebugReportFlagsEXT flags,
                                      VkDebugReportObjectTypeEXT objectType, uint64_t object,
                                      size_t location, int32_t messageCode,
                                      const char* pLayerPrefix, const char* pMessage,
                                      void* pUserData) {
    return reinterpret_cast<GfxSystem*>(pUserData)->HandleDebugReport(
        flags, objectType, object, location, messageCode, pLayerPrefix, pMessage);
  }

  VkBool32 HandleDebugReport(VkDebugReportFlagsEXT flags, VkDebugReportObjectTypeEXT objectType,
                             uint64_t object, size_t location, int32_t messageCode,
                             const char* pLayerPrefix, const char* pMessage);

  void DumpSessionMapResources(std::ostream& output,
                               std::unordered_set<GlobalId, GlobalId::Hash>* visited_resources);

  // TODO(SCN-452): Remove this when we externalize Displays.
  bool initialized_ = false;
  std::vector<fit::closure> run_after_initialized_;

  escher::VulkanInstancePtr vulkan_instance_;
  escher::VulkanDeviceQueuesPtr vulkan_device_queues_;
  vk::SurfaceKHR surface_;

  VkDebugReportCallbackEXT debug_report_callback_;

  std::optional<CommandContext> command_context_;

  // Tracks the number of sessions returning ApplyUpdateResult::needs_render
  // and uses it for tracing.
  uint64_t needs_render_count_ = 0;
  uint64_t processed_needs_render_count_ = 0;

  fxl::WeakPtrFactory<GfxSystem> weak_factory_;  // must be last
};

}  // namespace gfx
}  // namespace scenic_impl

#endif  // GARNET_LIB_UI_GFX_GFX_SYSTEM_H_
