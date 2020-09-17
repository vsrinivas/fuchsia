// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_EXAMPLES_FRAME_COMPRESSION_COMPUTE_VIEW_H_
#define SRC_UI_EXAMPLES_FRAME_COMPRESSION_COMPUTE_VIEW_H_

#include <fuchsia/sysmem/cpp/fidl.h>

#include "base_view.h"
#include "src/lib/fxl/macros.h"
#include "src/ui/lib/escher/escher.h"
#include "src/ui/lib/escher/impl/descriptor_set_pool.h"
#include "src/ui/lib/escher/vk/texture.h"

namespace frame_compression {

// Generates compressed image data using Vulkan compute.
class ComputeView : public BaseView {
 public:
  ComputeView(scenic::ViewContext context, escher::EscherWeakPtr weak_escher, uint64_t modifier,
              uint32_t width, uint32_t height, bool paint_once);
  ~ComputeView() override;

 private:
  struct Image {
    escher::SemaphorePtr acquire_semaphore;
    escher::SemaphorePtr release_semaphore;
    zx::event acquire_fence;
    zx::event release_fence;
    uint32_t image_id = 0;
    escher::TexturePtr texture;
    escher::BufferPtr buffer;
    uint32_t base_y = 0;
    uint32_t width_in_tiles = 0;
    uint32_t height_in_tiles = 0;
  };

  // |scenic::BaseView|
  void OnSceneInvalidated(fuchsia::images::PresentationInfo presentation_info) override;

  void RenderFrame(const Image& image, uint32_t color_offset, uint32_t frame_number);

  const escher::EscherWeakPtr escher_;
  const uint64_t modifier_;
  const bool paint_once_;
  escher::impl::DescriptorSetPool descriptor_set_pool_;
  fuchsia::sysmem::AllocatorSyncPtr sysmem_allocator_;
  Image images_[kNumImages];
  vk::Pipeline pipeline_;
  vk::PipelineLayout pipeline_layout_;

  FXL_DISALLOW_COPY_AND_ASSIGN(ComputeView);
};

}  // namespace frame_compression

#endif  // SRC_UI_EXAMPLES_FRAME_COMPRESSION_COMPUTE_VIEW_H_
