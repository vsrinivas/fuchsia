// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_EXAMPLES_FRAME_COMPRESSION_COMPUTE_VIEW_H_
#define SRC_UI_EXAMPLES_FRAME_COMPRESSION_COMPUTE_VIEW_H_

#include <fuchsia/sysmem/cpp/fidl.h>
#include <lib/inspect/cpp/inspect.h>
#include <lib/inspect/cpp/value_list.h>
#include <lib/inspect/cpp/vmo/types.h>

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
              uint32_t width, uint32_t height, uint32_t paint_count, uint32_t work_group_size,
              uint32_t work_group_tile_count, FILE* png_fp, inspect::Node inspect_node);
  ~ComputeView() override;

 private:
  static constexpr uint32_t kNumScratchImages = 2u;

  struct Image {
    escher::SemaphorePtr acquire_semaphore;
    escher::SemaphorePtr release_semaphore;
    zx::event acquire_fence;
    zx::event release_fence;
    uint32_t image_id;
    escher::TexturePtr texture;
    escher::BufferPtr buffer;
    escher::BufferPtr aux_buffer;
    escher::BufferPtr host_buffer;
    uint32_t body_offset = 0;
    uint32_t base_y = 0;
    uint32_t width_in_tiles = 0;
    uint32_t height_in_tiles = 0;
    inspect::LazyNode inspect_node;
  };

  // |scenic::BaseView|
  void OnSceneInvalidated(fuchsia::images::PresentationInfo presentation_info) override;

  void RenderFrameFromColorOffset(const Image& image, uint32_t color_offset, uint32_t frame_number);

  void RenderFrameFromPng(Image& image, png_structp png, uint32_t frame_number);

  uint32_t GetNextScratchImageIndex();

  fit::promise<inspect::Inspector> PopulateStats() const;
  fit::promise<inspect::Inspector> PopulateImageStats(const Image& image);

  const escher::EscherWeakPtr escher_;
  const uint64_t modifier_;
  const uint32_t paint_count_;
  const uint32_t work_group_tile_count_;
  FILE* const png_fp_;
  escher::impl::DescriptorSetPool descriptor_set_pool_;
  escher::impl::DescriptorSetPool pack_descriptor_set_pool_;
  fuchsia::sysmem::AllocatorSyncPtr sysmem_allocator_;
  Image images_[kNumImages];
  std::vector<png_bytep> row_pointers_;
  Image scratch_images_[kNumScratchImages];
  uint32_t next_scratch_image_index_ = 0;
  vk::PipelineLayout pipeline_layout_;
  vk::PipelineLayout pack_pipeline_layout_;
  vk::Pipeline pipeline_;
  vk::Pipeline pack_pipeline_;
  inspect::LazyNode inspect_node_;

  FXL_DISALLOW_COPY_AND_ASSIGN(ComputeView);
};

}  // namespace frame_compression

#endif  // SRC_UI_EXAMPLES_FRAME_COMPRESSION_COMPUTE_VIEW_H_
