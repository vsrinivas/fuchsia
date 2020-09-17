// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_EXAMPLES_FRAME_COMPRESSION_SOFTWARE_VIEW_H_
#define SRC_UI_EXAMPLES_FRAME_COMPRESSION_SOFTWARE_VIEW_H_

#include <fuchsia/sysmem/cpp/fidl.h>

#include "base_view.h"
#include "src/lib/fxl/macros.h"

namespace frame_compression {

class SoftwareView : public BaseView {
 public:
  SoftwareView(scenic::ViewContext context, uint64_t modifier, uint32_t width, uint32_t height,
               bool paint_once);
  ~SoftwareView() override = default;

 private:
  struct Image {
    uint32_t image_id;
    uint8_t* vmo_ptr;
    size_t image_bytes;
    uint32_t stride;
    bool needs_flush;
  };

  // |scenic::BaseView|
  void OnSceneInvalidated(fuchsia::images::PresentationInfo presentation_info) override;

  void SetPixels(const Image& image, uint32_t color_offset);
  void SetAfbcPixels(const Image& image, uint32_t color_offset);
  void SetLinearPixels(const Image& image, uint32_t color_offset);

  const uint64_t modifier_;
  const bool paint_once_;
  fuchsia::sysmem::AllocatorSyncPtr sysmem_allocator_;
  Image images_[kNumImages];

  FXL_DISALLOW_COPY_AND_ASSIGN(SoftwareView);
};

}  // namespace frame_compression

#endif  // SRC_UI_EXAMPLES_FRAME_COMPRESSION_SOFTWARE_VIEW_H_
