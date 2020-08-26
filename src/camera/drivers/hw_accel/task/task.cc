// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/camera/drivers/hw_accel/task/task.h"

#include <lib/syslog/global.h>
#include <stdint.h>
#include <zircon/pixelformat.h>
#include <zircon/types.h>

#include <memory>

constexpr auto kTag = "GenericTask";

namespace generictask {

// Validates the buffer collection.
static bool IsBufferCollectionValid(const buffer_collection_info_2_t* buffer_collection,
                                    const image_format_2_t* image_format) {
  return !(buffer_collection == nullptr || buffer_collection->buffer_count == 0 ||
           (image_format->pixel_format.type != fuchsia_sysmem_PixelFormatType_NV12 &&
            image_format->pixel_format.type != fuchsia_sysmem_PixelFormatType_R8G8B8A8));
}

zx_status_t GenericTask::GetInputBufferPhysAddr(uint32_t input_buffer_index,
                                                zx_paddr_t* out) const {
  if (input_buffer_index >= input_buffers_.size() || out == nullptr) {
    return ZX_ERR_INVALID_ARGS;
  }
  *out = input_buffers_[input_buffer_index].region(0).phys_addr;

  return ZX_OK;
}

zx_status_t GenericTask::GetInputBufferPhysSize(uint32_t input_buffer_index, uint64_t* out) const {
  if (input_buffer_index >= input_buffers_.size() || out == nullptr) {
    return ZX_ERR_INVALID_ARGS;
  }
  *out = input_buffers_[input_buffer_index].region(0).size;
  return ZX_OK;
}

zx_status_t GenericTask::InitBuffers(const buffer_collection_info_2_t* input_buffer_collection,
                                     const buffer_collection_info_2_t* output_buffer_collection,
                                     const image_format_2_t* input_image_format_table_list,
                                     size_t input_image_format_table_count,
                                     uint32_t input_image_format_index,
                                     const image_format_2_t* output_image_format_table_list,
                                     size_t output_image_format_table_count,
                                     uint32_t output_image_format_index, const zx::bti& bti,
                                     const hw_accel_frame_callback_t* frame_callback,
                                     const hw_accel_res_change_callback_t* res_callback,
                                     const hw_accel_remove_task_callback_t* remove_task_callback) {
  if (!IsBufferCollectionValid(output_buffer_collection,
                               &output_image_format_table_list[output_image_format_index])) {
    return ZX_ERR_INVALID_ARGS;
  }

  // Make a copy of the output image_format_table.
  output_image_format_list_ = std::make_unique<image_format_2_t[]>(output_image_format_table_count);
  for (uint32_t i = 0; i < output_image_format_table_count; i++) {
    output_image_format_list_.get()[i] = output_image_format_table_list[i];
  }
  output_image_format_count_ = output_image_format_table_count;
  cur_output_image_format_index_ = output_image_format_index;

  // Initialize the VMOPool and pin the output buffers
  zx::vmo output_vmos[output_buffer_collection->buffer_count];
  for (uint32_t i = 0; i < output_buffer_collection->buffer_count; ++i) {
    output_vmos[i] = zx::vmo(output_buffer_collection->buffers[i].vmo);
  }

  zx_status_t status = output_buffers_.Init(output_vmos, output_buffer_collection->buffer_count);
  if (status != ZX_OK) {
    FX_LOG(ERROR, kTag, "Unable to Init VmoPool");
    return status;
  }

  // Release the vmos so that the buffer collection could be reused.
  for (uint32_t i = 0; i < output_buffer_collection->buffer_count; ++i) {
    // output_buffer_collection already has the handle so its okay to discard
    // this one.
    __UNUSED zx_handle_t vmo = output_vmos[i].release();
  }

  status = output_buffers_.PinVmos(bti, fzl::VmoPool::RequireContig::Yes,
                                   fzl::VmoPool::RequireLowMem::Yes);
  if (status != ZX_OK) {
    FX_LOG(ERROR, kTag, "Unable to pin buffers");
    return status;
  }

  return InitInputBuffers(input_buffer_collection, input_image_format_table_list,
                          input_image_format_table_count, input_image_format_index, bti,
                          frame_callback, res_callback, remove_task_callback);
}

zx_status_t GenericTask::InitInputBuffers(
    const buffer_collection_info_2_t* input_buffer_collection,
    const image_format_2_t* input_image_format_table_list, size_t input_image_format_table_count,
    uint32_t input_image_format_index, const zx::bti& bti,
    const hw_accel_frame_callback_t* frame_callback,
    const hw_accel_res_change_callback_t* res_callback,
    const hw_accel_remove_task_callback_t* remove_task_callback) {
  if (!IsBufferCollectionValid(input_buffer_collection,
                               &input_image_format_table_list[input_image_format_index])) {
    return ZX_ERR_INVALID_ARGS;
  }

  input_image_format_list_ = std::make_unique<image_format_2_t[]>(input_image_format_table_count);
  for (uint32_t i = 0; i < input_image_format_table_count; i++) {
    input_image_format_list_.get()[i] = input_image_format_table_list[i];
  }
  input_image_format_count_ = input_image_format_table_count;
  cur_input_image_format_index_ = input_image_format_index;
  zx_status_t status = ZX_OK;

  // Pin the input buffers.
  input_buffers_ =
      fbl::Array<fzl::PinnedVmo>(new fzl::PinnedVmo[input_buffer_collection->buffer_count],
                                 input_buffer_collection->buffer_count);
  for (uint32_t i = 0; i < input_buffer_collection->buffer_count; ++i) {
    zx::vmo vmo(input_buffer_collection->buffers[i].vmo);
    status = input_buffers_[i].Pin(vmo, bti, ZX_BTI_CONTIGUOUS | ZX_VM_PERM_READ);

    // Release the vmos so that the buffer collection could be reused.
    // input_buffer_collection already has the handle so its okay to discard
    // this one.
    __UNUSED zx_handle_t handle = vmo.release();

    if (status != ZX_OK) {
      FX_LOG(ERROR, kTag, "Unable to pin buffers");
      return status;
    }
    if (input_buffers_[i].region_count() != 1) {
      FX_LOG(ERROR, kTag, "buffer is not contiguous");
      return ZX_ERR_NO_MEMORY;
    }
  }

  frame_callback_ = frame_callback;
  res_callback_ = res_callback;
  remove_task_callback_ = remove_task_callback;

  return status;
}

}  // namespace generictask
