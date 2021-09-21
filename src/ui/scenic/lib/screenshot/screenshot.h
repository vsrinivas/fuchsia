// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_SCENIC_LIB_SCREENSHOT_SCREENSHOT_H_
#define SRC_UI_SCENIC_LIB_SCREENSHOT_SCREENSHOT_H_

#include <fuchsia/ui/composition/cpp/fidl.h>
#include <lib/fidl/cpp/binding.h>

#include <unordered_map>

#include "src/ui/scenic/lib/allocation/buffer_collection_importer.h"
#include "src/ui/scenic/lib/flatland/engine/engine.h"
#include "src/ui/scenic/lib/flatland/renderer/vk_renderer.h"

namespace screenshot {

using Rectangle2D = escher::Rectangle2D;
using glm::vec2;
using GetRenderables = fit::function<
    std::pair<const std::vector<Rectangle2D>&, const std::vector<allocation::ImageMetadata>&>()>;

class Screenshot : public fuchsia::ui::composition::Screenshot {
 public:
  static std::vector<Rectangle2D> RotateRenderables(const std::vector<Rectangle2D>& rects,
                                                    fuchsia::ui::composition::Rotation rotation,
                                                    uint32_t image_width, uint32_t image_height);

  Screenshot(fidl::InterfaceRequest<fuchsia::ui::composition::Screenshot> request,
             uint32_t display_width, uint32_t display_height,
             const std::vector<std::shared_ptr<allocation::BufferCollectionImporter>>&
                 buffer_collection_importers,
             std::shared_ptr<flatland::VkRenderer> renderer, GetRenderables get_renderables);

  void CreateImage(fuchsia::ui::composition::CreateImageArgs args,
                   CreateImageCallback callback) override;

  void RemoveImage(fuchsia::ui::composition::RemoveImageArgs args,
                   RemoveImageCallback callback) override;

  void TakeScreenshot(fuchsia::ui::composition::TakeScreenshotArgs args,
                      TakeScreenshotCallback callback) override;

 private:
  // Clients cannot use zero as an Image ID.
  static constexpr int64_t kInvalidId = 0;

  fidl::Binding<fuchsia::ui::composition::Screenshot> binding_;

  uint32_t display_width_;
  uint32_t display_height_;

  std::vector<std::shared_ptr<allocation::BufferCollectionImporter>> buffer_collection_importers_;

  // Holds all registered images.
  std::unordered_map<int64_t, allocation::ImageMetadata> image_ids_;

  std::shared_ptr<flatland::VkRenderer> renderer_;
  GetRenderables get_renderables_;
};

}  // namespace screenshot

#endif  // SRC_UI_SCENIC_LIB_SCREENSHOT_SCREENSHOT_H_
