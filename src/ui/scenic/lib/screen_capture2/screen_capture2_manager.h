// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_SCENIC_LIB_SCREEN_CAPTURE2_SCREEN_CAPTURE2_MANAGER_H_
#define SRC_UI_SCENIC_LIB_SCREEN_CAPTURE2_SCREEN_CAPTURE2_MANAGER_H_

#include <fuchsia/ui/composition/cpp/fidl.h>
#include <lib/fidl/cpp/binding.h>
#include <lib/fidl/cpp/binding_set.h>

#include <unordered_map>

#include "screen_capture2.h"
#include "src/ui/scenic/lib/flatland/engine/engine.h"
#include "src/ui/scenic/lib/flatland/renderer/renderer.h"
#include "src/ui/scenic/lib/scheduling/frame_scheduler.h"
#include "src/ui/scenic/lib/screen_capture/screen_capture_buffer_collection_importer.h"

namespace screen_capture2 {

using LatchedTimes =
    const std::unordered_map<scheduling::SessionId,
                             std::map<scheduling::PresentId, /*latched_time=*/zx::time>>;
using SessionsToUpdate = const std::unordered_map<scheduling::SessionId, scheduling::PresentId>;

class ScreenCapture2Manager : public scheduling::SessionUpdater {
 public:
  ScreenCapture2Manager(std::shared_ptr<flatland::Renderer> renderer,
                        std::shared_ptr<screen_capture::ScreenCaptureBufferCollectionImporter>
                            screen_capture_buffer_collection_importer,
                        std::function<flatland::Renderables()> get_renderables_callback);
  ~ScreenCapture2Manager() override;

  void CreateClient(
      fidl::InterfaceRequest<fuchsia::ui::composition::internal::ScreenCapture> screen_capture);

  // |scheduling::SessionUpdater|
  scheduling::SessionUpdater::UpdateResults UpdateSessions(SessionsToUpdate& sessions_to_update,
                                                           uint64_t trace_id) override {
    return {};
  }

  // |scheduling::SessionUpdater|
  void OnCpuWorkDone() override;

  // |scheduling::SessionUpdater|
  void OnFramePresented(LatchedTimes& latched_times,
                        scheduling::PresentTimestamps present_times) override {}

  size_t client_count() const { return client_bindings_.size(); }

 private:
  std::shared_ptr<flatland::Renderer> renderer_;
  std::shared_ptr<screen_capture::ScreenCaptureBufferCollectionImporter>
      screen_capture_buffer_collection_importer_;

  fidl::BindingSet<fuchsia::ui::composition::internal::ScreenCapture,
                   std::unique_ptr<ScreenCapture>>
      client_bindings_;

  // Callback provided to pass to clients.
  std::function<flatland::Renderables()> get_renderables_callback_;
};

}  // namespace screen_capture2
#endif  // SRC_UI_SCENIC_LIB_SCREEN_CAPTURE2_SCREEN_CAPTURE2_MANAGER_H_
