// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_CAMERA_DRIVERS_HW_ACCEL_GE2D_GE2D_TASK_H_
#define SRC_CAMERA_DRIVERS_HW_ACCEL_GE2D_GE2D_TASK_H_

#include <deque>
#include <unordered_map>

#include <ddktl/protocol/amlogiccanvas.h>
#include <ddktl/protocol/ge2d.h>

#include "../task/task.h"

namespace ge2d {

static const uint8_t kYComponent = 0;
static const uint8_t kUVComponent = 1;
typedef struct image_canvas_id {
  uint8_t canvas_idx[2];
} image_canvas_id_t;

enum Ge2dTaskType { GE2D_RESIZE, GE2D_WATERMARK };

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
                         const hw_accel_callback_t* callback, const zx::bti& bti,
                         amlogic_canvas_protocol_t canvas);

  image_format_2_t WatermarkFormat() { return wm_.wm_image_format; }

  zx_status_t InitWatermark(const buffer_collection_info_2_t* input_buffer_collection,
                            const buffer_collection_info_2_t* output_buffer_collection,
                            const water_mark_info_t* info, const zx::vmo& watermark_vmo,
                            const image_format_2_t* input_image_format,
                            const image_format_2_t* output_image_format_table_list,
                            size_t output_image_format_table_count,
                            uint32_t output_image_format_index, const hw_accel_callback_t* callback,
                            const zx::bti& bti, amlogic_canvas_protocol_t canvas);

  image_canvas_id_t GetOutputCanvasIds(zx_handle_t vmo) {
    auto entry = buffer_map_.find(vmo);
    ZX_ASSERT(entry != buffer_map_.end());

    return entry->second;
  }

  image_canvas_id_t GetInputCanvasIds(uint32_t index) { return input_image_canvas_ids_[index]; }

  ~Ge2dTask() { FreeCanvasIds(); }

  void Ge2dChangeOutputRes(uint32_t new_output_buffer_index);

 private:
  zx_status_t Init(const buffer_collection_info_2_t* input_buffer_collection,
                   const buffer_collection_info_2_t* output_buffer_collection,
                   const image_format_2_t* input_image_format,
                   const image_format_2_t* output_image_format_table_list,
                   size_t output_image_format_table_count, uint32_t output_image_format_index,
                   const hw_accel_callback_t* callback, const zx::bti& bti);
  // Allocates canvas ids for every frame in the input and output buffer collections
  // (amlogic). One canvas id is allocated per plane of the image frame. Internally,
  // canvas id allocation pins the vmos (zx_bit_pin()).
  zx_status_t AllocCanvasIds(const buffer_collection_info_2_t* input_buffer_collection,
                             const buffer_collection_info_2_t* output_buffer_collection,
                             const image_format_2_t* input_image_format,
                             const image_format_2_t* output_image_format);
  zx_status_t AllocCanvasId(const image_format_2_t* image_format, zx_handle_t vmo_in,
                            image_canvas_id_t& canvas_ids, uint32_t alloc_flag);
  // Called from AllocCanvasIds() to allocate canvas ids for input and output buffer
  // collections.
  zx_status_t AllocInputCanvasIds(const buffer_collection_info_2_t* input_buffer_collection,
                                  const image_format_2_t* input_image_format);
  zx_status_t AllocOutputCanvasIds(const buffer_collection_info_2_t* output_buffer_collection,
                                   const image_format_2_t* output_image_format);
  void FreeCanvasIds();

  Ge2dTaskType task_type_;
  std::unique_ptr<image_format_2_t[]> output_image_format_list_;
  struct watermark_info {
    fzl::PinnedVmo watermark_vmo_pinned_;
    uint32_t loc_x;
    uint32_t loc_y;
    image_format_2_t wm_image_format;
  };
  watermark_info wm_;
  // Canvas id for the watermark image and the blended watermark image.
  // Both are RGBA images.
  uint8_t wm_input_canvas_id_;
  uint8_t wm_blended_canvas_id_;
  // Allocate a contig vmo to hold the input watermark image.
  zx::vmo watermark_input_vmo_;
  // vmo to hold blended watermark image.
  zx::vmo watermark_blended_vmo_;
  resize_info_t res_info_;
  std::unordered_map<zx_handle_t, image_canvas_id_t> buffer_map_;
  uint32_t num_input_canvas_ids_;
  std::unique_ptr<image_canvas_id_t[]> input_image_canvas_ids_;
  amlogic_canvas_protocol_t canvas_ = {};
};
}  // namespace ge2d

#endif  // SRC_CAMERA_DRIVERS_HW_ACCEL_GE2D_GE2D_TASK_H_
