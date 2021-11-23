// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "screenshot_manager.h"

#include <lib/syslog/cpp/macros.h>

#include "rapidjson/document.h"
#include "screenshot.h"
#include "src/lib/files/file.h"
#include "src/lib/fsl/handles/object_info.h"
#include "src/ui/scenic/lib/flatland/engine/engine.h"
#include "src/ui/scenic/lib/flatland/renderer/vk_renderer.h"

namespace screenshot {
ScreenshotManager::ScreenshotManager(
    std::shared_ptr<flatland::Engine> engine, std::shared_ptr<flatland::VkRenderer> renderer,
    std::shared_ptr<flatland::FlatlandManager> flatland_manager,
    std::vector<std::shared_ptr<allocation::BufferCollectionImporter>> buffer_collection_importers)
    : engine_(engine),
      renderer_(renderer),
      flatland_manager_(flatland_manager),
      buffer_collection_importers_(std::move(buffer_collection_importers)) {
  FX_DCHECK(engine_);
  FX_DCHECK(renderer_);
  FX_DCHECK(flatland_manager_);
}

void ScreenshotManager::CreateClient(
    fidl::InterfaceRequest<fuchsia::ui::composition::Screenshot> request) {
  const auto id = next_client_id_++;

  std::unique_ptr<Screenshot> screenshot = std::make_unique<Screenshot>(
      std::move(request), buffer_collection_importers_, renderer_, [this]() {
        FX_DCHECK(flatland_manager_);
        FX_DCHECK(engine_);

        auto display = flatland_manager_->GetPrimaryFlatlandDisplayForRendering();
        FX_DCHECK(display);

        return engine_->GetRenderables(display->root_transform());
      });

  screenshot_clients_[id] = std::move(screenshot);
}

}  // namespace screenshot
