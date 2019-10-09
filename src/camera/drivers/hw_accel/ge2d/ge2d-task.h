// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_CAMERA_DRIVERS_HW_ACCEL_GE2D_GE2D_TASK_H_
#define SRC_CAMERA_DRIVERS_HW_ACCEL_GE2D_GE2D_TASK_H_

#include <ddktl/protocol/ge2d.h>

#include "../task/task.h"

namespace ge2d {
class Ge2dTask : public generictask::GenericTask {
 public:
  // Static function to create a task object.
  // |input_buffer_collection|              : Input buffer collection.
  // |output_buffer_collection|             : Output buffer collection.
  // [info]                                 : Either Resize or Watermark Info.
  // [image_format_table_list]              : List of image formats.
  // [image_format_table_count]             : Size of image format table.
  // [image_format_index]                   : Index of image format to initialize with.
  // |callback|                             : Callback function to call for.
  // this task. |out|                       : Pointer to a task.
  // object returned to the caller.
  zx_status_t InitResize(const buffer_collection_info_2_t* input_buffer_collection,
                         const buffer_collection_info_2_t* output_buffer_collection,
                         const resize_info_t* info, const image_format_2_t* input_image_format,
                         const image_format_2_t* output_image_format_table_list,
                         size_t output_image_format_table_count, uint32_t output_image_format_index,
                         const hw_accel_callback_t* callback, const zx::bti& bti);

  zx_status_t InitWatermark(const buffer_collection_info_2_t* input_buffer_collection,
                            const buffer_collection_info_2_t* output_buffer_collection,
                            const water_mark_info_t* info, const zx::vmo& watermark_vmo,
                            const image_format_2_t* input_image_format,
                            const image_format_2_t* output_image_format_table_list,
                            size_t output_image_format_table_count,
                            uint32_t output_image_format_index, const hw_accel_callback_t* callback,
                            const zx::bti& bti);

 private:
  zx_status_t Init(const buffer_collection_info_2_t* input_buffer_collection,
                   const buffer_collection_info_2_t* output_buffer_collection,
                   const image_format_2_t* input_image_format,
                   const image_format_2_t* output_image_format_table_list,
                   size_t output_image_format_table_count, uint32_t output_image_format_index,
                   const hw_accel_callback_t* callback, const zx::bti& bti);
  zx_status_t PinWatermarkVmo(const zx::vmo& watermark_vmo, const zx::bti& bti);
  std::unique_ptr<image_format_2_t[]> output_image_format_list_;
  struct watermark_info {
    fzl::PinnedVmo watermark_vmo_pinned_;
    uint32_t loc_x;
    uint32_t loc_y;
    uint32_t size_width;
    uint32_t size_height;
  };
  watermark_info wm_;
  resize_info_t res_info_;
};
}  // namespace ge2d

#endif  // SRC_CAMERA_DRIVERS_HW_ACCEL_GE2D_GE2D_TASK_H_
