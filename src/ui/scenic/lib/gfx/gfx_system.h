// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_SCENIC_LIB_GFX_GFX_SYSTEM_H_
#define SRC_UI_SCENIC_LIB_GFX_GFX_SYSTEM_H_

#include <memory>

#include <trace/event.h>

#include "src/ui/lib/escher/escher.h"
#include "src/ui/scenic/lib/gfx/engine/engine.h"
#include "src/ui/scenic/lib/gfx/engine/gfx_command_applier.h"
#include "src/ui/scenic/lib/scenic/system.h"
#include "src/ui/scenic/lib/scenic/take_screenshot_delegate_deprecated.h"

namespace scenic_impl {
namespace gfx {

class GfxSystem;
using GfxSystemWeakPtr = fxl::WeakPtr<GfxSystem>;

class GfxSystem : public System,
                  public scenic_impl::TakeScreenshotDelegateDeprecated,
                  public scheduling::SessionUpdater {
 public:
  static constexpr TypeId kTypeId = kGfx;
  static const char* kName;

  GfxSystem(SystemContext context, Engine* engine, escher::EscherWeakPtr escher, Sysmem* sysmem,
            display::DisplayManager* display_manager);

  GfxSystemWeakPtr GetWeakPtr() { return weak_factory_.GetWeakPtr(); }

  CommandDispatcherUniquePtr CreateCommandDispatcher(
      scheduling::SessionId session_id, std::shared_ptr<EventReporter> event_reporter,
      std::shared_ptr<ErrorReporter> error_reporter) override;

  // TODO(fxb/40795): Remove this.
  // |scenic_impl::TakeScreenshotDelegateDeprecated|
  void TakeScreenshot(fuchsia::ui::scenic::Scenic::TakeScreenshotCallback callback) override;

  // |scheduling::SessionUpdater|
  virtual UpdateResults UpdateSessions(
      const std::unordered_map<scheduling::SessionId, scheduling::PresentId>& sessions_to_update,
      uint64_t trace_id) override;

  // |scheduling::SessionUpdater|
  virtual void PrepareFrame(uint64_t trace_id) override;

  // For tests.
  SessionManager* session_manager() { return &session_manager_; }

  static escher::EscherUniquePtr CreateEscher(sys::ComponentContext* app_context);

 private:
  static VkBool32 HandleDebugReport(VkDebugReportFlagsEXT flags,
                                    VkDebugReportObjectTypeEXT objectType, uint64_t object,
                                    size_t location, int32_t messageCode, const char* pLayerPrefix,
                                    const char* pMessage, void* pUserData);

  void DumpSessionMapResources(std::ostream& output,
                               std::unordered_set<GlobalId, GlobalId::Hash>* visited_resources);

  display::DisplayManager* const display_manager_;

  Sysmem* const sysmem_;
  escher::EscherWeakPtr escher_;

  Engine* const engine_;
  SessionManager session_manager_;

  std::optional<CommandContext> command_context_;

  fxl::WeakPtrFactory<GfxSystem> weak_factory_;  // must be last
};

}  // namespace gfx
}  // namespace scenic_impl

#endif  // SRC_UI_SCENIC_LIB_GFX_GFX_SYSTEM_H_
