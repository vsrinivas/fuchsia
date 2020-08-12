// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_EXAMPLES_FRAME_COMPRESSION_BASE_VIEW_H_
#define SRC_UI_EXAMPLES_FRAME_COMPRESSION_BASE_VIEW_H_

#include <lib/syslog/cpp/macros.h>
#include <lib/ui/scenic/cpp/resources.h>

#include "src/lib/fxl/macros.h"
#include "src/lib/ui/base_view/base_view.h"

namespace frame_compression {

// Base class for examples that generate compressed frames using the CPU,
// or by Vulkan compute.
class BaseView : public scenic::BaseView {
 public:
  BaseView(scenic::ViewContext context, const std::string& debug_name, uint32_t width,
           uint32_t height);
  ~BaseView() override = default;

 protected:
  static constexpr uint32_t kAfbcBodyAlignment = 1024u;
  static constexpr uint32_t kAfbcBytesPerBlockHeader = 16u;
  static constexpr uint32_t kAfbcTilePixelWidth = 16u;
  static constexpr uint32_t kAfbcTilePixelHeight = 16u;
  static constexpr uint32_t kAfbcSubtileSize = 4u;
  static constexpr uint32_t kTileBytesPerPixel = 4u;
  static constexpr uint32_t kNumImages = 3u;

  const uint32_t width_;
  const uint32_t height_;
  uint32_t next_image_pipe_id_ = 1;
  fidl::InterfacePtr<fuchsia::images::ImagePipe2> image_pipe_;

  uint32_t GetNextImageIndex();
  // Returns the color offset used for the producing contents for the next frame.
  // The color offset determines at what Y offset we should switch from first
  // to second color.
  uint32_t GetNextColorOffset();

 private:
  // |scenic::BaseView|
  // Called when the scene is "invalidated". Invalidation happens when surface
  // dimensions or metrics change, but not necessarily when surface contents
  // change.
  void OnSceneInvalidated(fuchsia::images::PresentationInfo presentation_info) override;

  // |scenic::SessionListener|
  void OnScenicError(std::string error) override { FX_LOGS(ERROR) << "Scenic Error " << error; }

  uint32_t next_color_offset_ = 0;
  uint32_t next_image_index_ = 0;
  scenic::ShapeNode node_;

  FXL_DISALLOW_COPY_AND_ASSIGN(BaseView);
};

}  // namespace frame_compression

#endif  // SRC_UI_EXAMPLES_FRAME_COMPRESSION_BASE_VIEW_H_
