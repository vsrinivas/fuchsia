// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_SCENIC_LIB_SCREEN_CAPTURE_SCREEN_CAPTURE_H_
#define SRC_UI_SCENIC_LIB_SCREEN_CAPTURE_SCREEN_CAPTURE_H_

#include <fuchsia/ui/composition/cpp/fidl.h>
#include <lib/fidl/cpp/binding.h>

#include <unordered_map>

#include "src/ui/scenic/lib/allocation/buffer_collection_importer.h"
#include "src/ui/scenic/lib/flatland/engine/engine.h"
#include "src/ui/scenic/lib/flatland/flatland_types.h"
#include "src/ui/scenic/lib/flatland/renderer/renderer.h"

using glm::vec2;
using GetRenderables = std::function<flatland::Renderables()>;

namespace screen_capture {

class ScreenCapture : public fuchsia::ui::composition::ScreenCapture {
 public:
  static std::vector<flatland::ImageRect> RotateRenderables(
      const std::vector<flatland::ImageRect>& rects, fuchsia::ui::composition::Rotation rotation,
      uint32_t image_width, uint32_t image_height);

  ScreenCapture(fidl::InterfaceRequest<fuchsia::ui::composition::ScreenCapture> request,
                const std::vector<std::shared_ptr<allocation::BufferCollectionImporter>>&
                    buffer_collection_importers,
                std::shared_ptr<flatland::Renderer> renderer, GetRenderables get_renderables);

  ~ScreenCapture() override;

  void Configure(fuchsia::ui::composition::ScreenCaptureConfig args,
                 ConfigureCallback callback) override;

  void GetNextFrame(fuchsia::ui::composition::GetNextFrameArgs args,
                    GetNextFrameCallback callback) override;

  void ReleaseFrame(uint32_t buffer_id, ReleaseFrameCallback callback) override;

 private:
  void ClearImages();

  // Clients cannot use zero as an Image ID.
  static constexpr int64_t kInvalidId = 0;

  fidl::Binding<fuchsia::ui::composition::ScreenCapture> binding_;

  std::vector<std::shared_ptr<allocation::BufferCollectionImporter>> buffer_collection_importers_;

  // Holds all registered images.
  std::unordered_map<int64_t, allocation::ImageMetadata> image_ids_;

  // Indices of available buffers.
  std::deque<uint32_t> available_buffers_;

  fuchsia::ui::composition::Rotation stream_rotation_;

  std::shared_ptr<flatland::Renderer> renderer_;
  GetRenderables get_renderables_;
};

}  // namespace screen_capture

#endif  // SRC_UI_SCENIC_LIB_SCREEN_CAPTURE_SCREEN_CAPTURE_H_
