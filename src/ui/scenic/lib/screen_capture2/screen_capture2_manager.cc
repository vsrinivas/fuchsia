// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "screen_capture2_manager.h"

#include <lib/syslog/cpp/macros.h>

#include <memory>

#include "src/lib/files/file.h"
#include "src/lib/fsl/handles/object_info.h"
#include "src/lib/fxl/memory/weak_ptr.h"
#include "src/ui/scenic/lib/flatland/engine/engine.h"
#include "src/ui/scenic/lib/flatland/renderer/renderer.h"
#include "src/ui/scenic/lib/screen_capture2/screen_capture2.h"

namespace screen_capture2 {

ScreenCapture2Manager::ScreenCapture2Manager(
    std::shared_ptr<flatland::Renderer> renderer,
    std::shared_ptr<screen_capture::ScreenCaptureBufferCollectionImporter>
        screen_capture_buffer_collection_importer,
    std::function<flatland::Renderables()> get_renderables_callback)
    : renderer_(renderer),
      screen_capture_buffer_collection_importer_(screen_capture_buffer_collection_importer),
      get_renderables_callback_(get_renderables_callback) {
  FX_DCHECK(renderer_);
  FX_DCHECK(screen_capture_buffer_collection_importer_);
  FX_DCHECK(get_renderables_callback_);
}

ScreenCapture2Manager::~ScreenCapture2Manager() { client_bindings_.CloseAll(); }

void ScreenCapture2Manager::CreateClient(
    fidl::InterfaceRequest<fuchsia::ui::composition::internal::ScreenCapture> request) {
  std::unique_ptr<ScreenCapture> instance = std::make_unique<ScreenCapture>(
      screen_capture_buffer_collection_importer_, renderer_,
      /*get_renderables=*/[this]() { return get_renderables_callback_(); });
  client_bindings_.AddBinding(std::move(instance), std::move(request));
}

void ScreenCapture2Manager::OnCpuWorkDone() {
  // After the newest batch of renderables has been produced, loop through all of the bindings and
  // render into the client's buffer if they have requested one.
  for (const auto& binding : client_bindings_.bindings()) {
    binding.get()->impl()->MaybeRenderFrame();
  }
}

}  // namespace screen_capture2
