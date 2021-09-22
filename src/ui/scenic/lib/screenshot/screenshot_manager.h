// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_SCENIC_LIB_SCREENSHOT_SCREENSHOT_MANAGER_H_
#define SRC_UI_SCENIC_LIB_SCREENSHOT_SCREENSHOT_MANAGER_H_

#include <fuchsia/ui/composition/cpp/fidl.h>
#include <lib/fidl/cpp/binding.h>

#include <unordered_map>

#include "screenshot.h"
#include "screenshot_buffer_collection_importer.h"
#include "src/ui/scenic/lib/allocation/buffer_collection_importer.h"
#include "src/ui/scenic/lib/flatland/engine/engine.h"
#include "src/ui/scenic/lib/flatland/renderer/vk_renderer.h"

namespace screenshot {

using ClientId = uint64_t;

class ScreenshotManager {
 public:
  ScreenshotManager(std::shared_ptr<flatland::Engine> engine,
                    std::shared_ptr<flatland::VkRenderer> renderer,
                    std::shared_ptr<flatland::FlatlandDisplay> display,
                    std::vector<std::shared_ptr<allocation::BufferCollectionImporter>>
                        buffer_collection_importers);

  void CreateClient(fidl::InterfaceRequest<fuchsia::ui::composition::Screenshot> screenshot);

 private:
  uint32_t display_width_;
  uint32_t display_height_;

  // We need these for rendering the scene into the client supplied buffer.
  std::shared_ptr<flatland::Engine> engine_;
  std::shared_ptr<flatland::VkRenderer> renderer_;
  std::shared_ptr<flatland::FlatlandDisplay> display_;
  std::vector<std::shared_ptr<allocation::BufferCollectionImporter>> buffer_collection_importers_;

  ClientId next_client_id_ = 1;

  std::unordered_map<ClientId, std::unique_ptr<Screenshot>> screenshot_clients_;
};

}  // namespace screenshot
#endif  // SRC_UI_SCENIC_LIB_SCREENSHOT_SCREENSHOT_MANAGER_H_
