// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ge2d-task.h"

#include <lib/syslog/global.h>
#include <stdint.h>
#include <zircon/pixelformat.h>
#include <zircon/types.h>

#include <memory>

#include <ddk/debug.h>
#include <fbl/alloc_checker.h>

constexpr uint32_t kEndianness = 7;
constexpr auto TAG = "ge2d";

namespace ge2d {

zx_status_t Ge2dTask::AllocCanvasId(const image_format_2_t* image_format, zx_handle_t vmo_in,
                                    image_canvas_id_t& canvas_ids, uint32_t alloc_flag) {
  canvas_info_t info;
  info.height = image_format->display_height;
  info.stride_bytes = image_format->bytes_per_row;
  info.wrap = 0;
  info.blkmode = 0;
  // Do 64-bit endianness conversion.
  info.endianness = kEndianness;
  info.flags = alloc_flag;
  zx_status_t status;
  zx_handle_t vmo_dup;

  // TODO: (Bug 39820) dup'ing the vmo handle here seems unnecessary. Amlogic
  // canvas config calls zx_bit_pin(), which will keep a reference to the vmo
  // (and drop the reference on the unpin()). We should therefore be able to
  // pass the same vmo for the 2 canvas config calls, a reference will be held
  // for each pin. OTOH, dup'ing the vmo for canvas id allocations for NV12 is
  // what the vim display code and other code does, so I am following that up
  // here.
  status = zx_handle_duplicate(vmo_in, ZX_RIGHT_SAME_RIGHTS, &vmo_dup);
  if (status != ZX_OK) {
    return status;
  }
  status = amlogic_canvas_config(&canvas_, vmo_dup, 0,  // offset of plane 0 is at 0.
                                 &info, &canvas_ids.canvas_idx[kYComponent]);
  info.height /= 2;  // For NV12, second plane height is 1/2 first.
  status = amlogic_canvas_config(&canvas_, vmo_in,
                                 image_format->display_height * image_format->bytes_per_row, &info,
                                 &canvas_ids.canvas_idx[kUVComponent]);
  if (status != ZX_OK) {
    amlogic_canvas_free(&canvas_, canvas_ids.canvas_idx[kYComponent]);
    return ZX_ERR_NO_RESOURCES;
  }
  return ZX_OK;
}

zx_status_t Ge2dTask::AllocInputCanvasIds(const buffer_collection_info_2_t* input_buffer_collection,
                                          const image_format_2_t* input_image_format) {
  if (input_image_format->pixel_format.type != fuchsia_sysmem_PixelFormatType_NV12) {
    return ZX_ERR_NOT_SUPPORTED;
  }
  if (((input_image_format->display_height % 2) != 0) || (input_image_format->bytes_per_row == 0)) {
    return ZX_ERR_INVALID_ARGS;
  }
  num_input_canvas_ids_ = 0;
  fbl::AllocChecker ac;
  std::unique_ptr<input_image_canvas_id_t[]> input_image_canvas_ids;
  input_image_canvas_ids = std::unique_ptr<input_image_canvas_id_t[]>(
      new (&ac) input_image_canvas_id_t[input_buffer_collection->buffer_count]);
  if (!ac.check()) {
    return ZX_ERR_NO_MEMORY;
  }
  zx_status_t status;
  for (uint32_t i = 0; i < input_buffer_collection->buffer_count; i++) {
    status = AllocCanvasId(input_image_format, input_buffer_collection->buffers[i].vmo,
                           input_image_canvas_ids[i].canvas_ids, CANVAS_FLAGS_READ);
    if (status != ZX_OK) {
      for (uint32_t j = 0; j < i; j++) {
        amlogic_canvas_free(&canvas_, input_image_canvas_ids[j].canvas_ids.canvas_idx[kYComponent]);
        amlogic_canvas_free(&canvas_,
                            input_image_canvas_ids[j].canvas_ids.canvas_idx[kUVComponent]);
        zx_handle_close(input_image_canvas_ids[j].vmo);
      }
      return status;
    }
    // Canvas id allocation was successful. Dup the vmo handle and save it along with
    // the canvas ids. We need the vmo handle when we change the input resoultion.
    zx_handle_t vmo_dup;
    status = zx_handle_duplicate(input_buffer_collection->buffers[i].vmo, ZX_RIGHT_SAME_RIGHTS,
                                 &vmo_dup);
    if (status != ZX_OK) {
      for (uint32_t j = 0; j < i; j++) {
        amlogic_canvas_free(&canvas_, input_image_canvas_ids[j].canvas_ids.canvas_idx[kYComponent]);
        amlogic_canvas_free(&canvas_,
                            input_image_canvas_ids[j].canvas_ids.canvas_idx[kUVComponent]);
        zx_handle_close(input_image_canvas_ids[j].vmo);
      }
      return status;
    }
    input_image_canvas_ids[i].vmo = vmo_dup;
  }
  num_input_canvas_ids_ = input_buffer_collection->buffer_count;
  input_image_canvas_ids_ = move(input_image_canvas_ids);
  return ZX_OK;
}

// Allocation of output buffer canvas ids is a bit more involved. We need to
// allocate the canvas ids and then insert them in a hashmap, where we can look
// up by the vmo (handle) the underlying buffer.
zx_status_t Ge2dTask::AllocOutputCanvasIds(
    const buffer_collection_info_2_t* output_buffer_collection,
    const image_format_2_t* output_image_format) {
  if (output_image_format->pixel_format.type != fuchsia_sysmem_PixelFormatType_NV12) {
    return ZX_ERR_NOT_SUPPORTED;
  }
  if (((output_image_format->display_height % 2) != 0) ||
      (output_image_format->bytes_per_row == 0)) {
    return ZX_ERR_INVALID_ARGS;
  }
  // Create a map from <vmo handle> -> <canvas id pair> for every output
  // buffer. We do this by allocating each output buffer, allocating a
  // canvas id pair for it, adding that to the hashmap and then freeing
  // the buffers when done.
  typedef struct buf_canvasids {
    fzl::VmoPool::Buffer output_buffer;
    image_canvas_id_t canvas_ids;
  } buf_canvas_ids_t;
  std::unique_ptr<buf_canvas_ids_t[]> buf_canvas_ids;
  fbl::AllocChecker ac;
  buf_canvas_ids = std::unique_ptr<buf_canvas_ids_t[]>(
      new (&ac) buf_canvas_ids_t[output_buffer_collection->buffer_count]);
  if (!ac.check()) {
    return ZX_ERR_NO_MEMORY;
  }
  for (uint32_t i = 0; i < output_buffer_collection->buffer_count; i++) {
    buf_canvas_ids[i].output_buffer = WriteLockOutputBuffer();
    zx_handle_t vmo_handle = buf_canvas_ids[i].output_buffer.vmo_handle();
    zx_status_t status =
        AllocCanvasId(output_image_format, vmo_handle, buf_canvas_ids[i].canvas_ids,
                      CANVAS_FLAGS_READ | CANVAS_FLAGS_WRITE);
    if (status != ZX_OK) {
      for (uint32_t j = 0; j < i; j++) {
        amlogic_canvas_free(&canvas_, buf_canvas_ids[j].canvas_ids.canvas_idx[kYComponent]);
        amlogic_canvas_free(&canvas_, buf_canvas_ids[j].canvas_ids.canvas_idx[kUVComponent]);
        ReleaseOutputBuffer(std::move(buf_canvas_ids[j].output_buffer));
      }
      return status;
    }
  }
  for (uint32_t i = 0; i < output_buffer_collection->buffer_count; i++) {
    buffer_map_[buf_canvas_ids[i].output_buffer.vmo_handle()] = buf_canvas_ids[i].canvas_ids;
    ReleaseOutputBuffer(std::move(buf_canvas_ids[i].output_buffer));
  }
  return ZX_OK;
}

zx_status_t Ge2dTask::AllocCanvasIds(const buffer_collection_info_2_t* input_buffer_collection,
                                     const buffer_collection_info_2_t* output_buffer_collection,
                                     const image_format_2_t* input_image_format,
                                     const image_format_2_t* output_image_format) {
  zx_status_t status = AllocInputCanvasIds(input_buffer_collection, input_image_format);
  if (status != ZX_OK) {
    return status;
  }
  status = AllocOutputCanvasIds(output_buffer_collection, output_image_format);
  return status;
}

void Ge2dTask::FreeCanvasIds() {
  for (uint32_t j = 0; j < num_input_canvas_ids_; j++) {
    amlogic_canvas_free(&canvas_, input_image_canvas_ids_[j].canvas_ids.canvas_idx[kYComponent]);
    amlogic_canvas_free(&canvas_, input_image_canvas_ids_[j].canvas_ids.canvas_idx[kUVComponent]);
    zx_handle_close(input_image_canvas_ids_[j].vmo);
  }
  num_input_canvas_ids_ = 0;
  for (auto it = buffer_map_.cbegin(); it != buffer_map_.cend(); ++it) {
    amlogic_canvas_free(&canvas_, it->second.canvas_idx[kYComponent]);
    amlogic_canvas_free(&canvas_, it->second.canvas_idx[kUVComponent]);
  }
  if (task_type_ == GE2D_WATERMARK) {
    amlogic_canvas_free(&canvas_, wm_input_canvas_id_);
    amlogic_canvas_free(&canvas_, wm_blended_canvas_id_);
  }
}

void Ge2dTask::Ge2dChangeOutputRes(uint32_t new_output_buffer_index) {
  set_output_format_index(new_output_buffer_index);
  // Re-allocate the Output canvas IDs.
  image_format_2_t format = output_format();
  for (auto& it : buffer_map_) {
    image_canvas_id_t canvas_ids;
    zx_status_t status =
        AllocCanvasId(&format, it.first, canvas_ids, CANVAS_FLAGS_READ | CANVAS_FLAGS_WRITE);
    ZX_ASSERT(status == ZX_OK);
    // Free old canvas ids.
    amlogic_canvas_free(&canvas_, it.second.canvas_idx[kYComponent]);
    amlogic_canvas_free(&canvas_, it.second.canvas_idx[kUVComponent]);
    it.second = canvas_ids;
  }
}

void Ge2dTask::Ge2dChangeInputRes(uint32_t new_input_buffer_index) {
  set_input_format_index(new_input_buffer_index);
  // Re-allocate the Input canvas IDs.
  image_format_2_t format = input_format();
  for (uint32_t j = 0; j < num_input_canvas_ids_; j++) {
    image_canvas_id_t canvas_ids;
    zx_status_t status = AllocCanvasId(&format, input_image_canvas_ids_[j].vmo, canvas_ids,
                                       CANVAS_FLAGS_READ | CANVAS_FLAGS_WRITE);
    ZX_ASSERT(status == ZX_OK);
    amlogic_canvas_free(&canvas_, input_image_canvas_ids_[j].canvas_ids.canvas_idx[kYComponent]);
    amlogic_canvas_free(&canvas_, input_image_canvas_ids_[j].canvas_ids.canvas_idx[kUVComponent]);
    input_image_canvas_ids_[j].canvas_ids = canvas_ids;
  }
}

zx_status_t Ge2dTask::Init(const buffer_collection_info_2_t* input_buffer_collection,
                           const buffer_collection_info_2_t* output_buffer_collection,
                           const image_format_2_t* input_image_format_table_list,
                           size_t input_image_format_table_count, uint32_t input_image_format_index,
                           const image_format_2_t* output_image_format_table_list,
                           size_t output_image_format_table_count,
                           uint32_t output_image_format_index,
                           const hw_accel_frame_callback_t* frame_callback,
                           const hw_accel_res_change_callback_t* res_callback, const zx::bti& bti) {
  if ((output_image_format_table_count < 1) ||
      (output_image_format_index >= output_image_format_table_count) ||
      (input_image_format_table_count < 1) ||
      (input_image_format_index >= input_image_format_table_count) || (frame_callback == nullptr) ||
      (res_callback == nullptr)) {
    return ZX_ERR_INVALID_ARGS;
  }

  zx_status_t status =
      InitBuffers(input_buffer_collection, output_buffer_collection, input_image_format_table_list,
                  input_image_format_table_count, input_image_format_index,
                  output_image_format_table_list, output_image_format_table_count,
                  output_image_format_index, bti, frame_callback, res_callback);
  if (status != ZX_OK) {
    FX_LOG(ERROR, TAG, "InitBuffers Failed");
    return status;
  }

  status = AllocCanvasIds(input_buffer_collection, output_buffer_collection,
                          &input_image_format_table_list[input_image_format_index],
                          &output_image_format_table_list[output_image_format_index]);

  return status;
}

zx_status_t Ge2dTask::InitResize(const buffer_collection_info_2_t* input_buffer_collection,
                                 const buffer_collection_info_2_t* output_buffer_collection,
                                 const resize_info_t* info,
                                 const image_format_2_t* input_image_format,
                                 const image_format_2_t* output_image_format_table_list,
                                 size_t output_image_format_table_count,
                                 uint32_t output_image_format_index,
                                 const hw_accel_frame_callback_t* frame_callback,
                                 const hw_accel_res_change_callback_t* res_callback,
                                 const zx::bti& bti, amlogic_canvas_protocol_t canvas) {
  canvas_ = canvas;

  zx_status_t status = Init(input_buffer_collection, output_buffer_collection, input_image_format,
                            1, 0, output_image_format_table_list, output_image_format_table_count,
                            output_image_format_index, frame_callback, res_callback, bti);
  if (status != ZX_OK) {
    FX_LOG(ERROR, TAG, "Init Failed");
    return status;
  }

  // Make a copy of the resize info
  res_info_ = *info;

  task_type_ = GE2D_RESIZE;

  return status;
}

zx_status_t Ge2dTask::InitWatermark(const buffer_collection_info_2_t* input_buffer_collection,
                                    const buffer_collection_info_2_t* output_buffer_collection,
                                    const water_mark_info_t* wm_info, const zx::vmo& watermark_vmo,
                                    const image_format_2_t* image_format_table_list,
                                    size_t image_format_table_count, uint32_t image_format_index,
                                    const hw_accel_frame_callback_t* frame_callback,
                                    const hw_accel_res_change_callback_t* res_callback,
                                    const zx::bti& bti, amlogic_canvas_protocol_t canvas) {
  canvas_ = canvas;

  zx_status_t status =
      Init(input_buffer_collection, output_buffer_collection, image_format_table_list,
           image_format_table_count, image_format_index, image_format_table_list,
           image_format_table_count, image_format_index, frame_callback, res_callback, bti);
  if (status != ZX_OK) {
    FX_LOG(ERROR, TAG, "Init Failed");
    return status;
  }

  if (wm_info->wm_image_format.pixel_format.type != fuchsia_sysmem_PixelFormatType_R8G8B8A8) {
    FX_LOG(ERROR, TAG, "Image format type not supported");
    return ZX_ERR_NOT_SUPPORTED;
  }

  // Make copy of watermark info, pin watermark vmo.
  wm_.loc_x = wm_info->loc_x;
  wm_.loc_y = wm_info->loc_y;
  wm_.wm_image_format = wm_info->wm_image_format;

  task_type_ = GE2D_WATERMARK;

  uint64_t vmo_size;
  vmo_size = wm_.wm_image_format.display_height * wm_.wm_image_format.bytes_per_row;

  // The watermark vmo may not necessarily be contig. Allocate a contig vmo and
  // copy the contents of the watermark image into it and use that.
  status = zx::vmo::create_contiguous(bti, vmo_size, 0, &watermark_input_vmo_);
  if (status != ZX_OK) {
    FX_LOG(ERROR, TAG, "Unable to get create contiguous input watermark VMO");
    return status;
  }
  // Copy the watermark image over.
  fzl::VmoMapper mapped_watermark_input_vmo;
  status = mapped_watermark_input_vmo.Map(watermark_vmo, 0, vmo_size, ZX_VM_PERM_READ);
  if (status != ZX_OK) {
    FX_LOG(ERROR, TAG, "Unable to get map for watermark input VMO");
    return status;
  }
  fzl::VmoMapper mapped_contig_vmo;
  status =
      mapped_contig_vmo.Map(watermark_input_vmo_, 0, vmo_size, ZX_VM_PERM_READ | ZX_VM_PERM_WRITE);
  if (status != ZX_OK) {
    FX_LOG(ERROR, TAG, "Unable to get map contig watermark VMO");
    return status;
  }
  memcpy(mapped_contig_vmo.start(), mapped_watermark_input_vmo.start(), vmo_size);

  // Allocate input watermark canvas id.
  canvas_info_t info;
  info.height = wm_.wm_image_format.display_height;
  info.stride_bytes = wm_.wm_image_format.bytes_per_row;
  info.wrap = 0;
  info.blkmode = 0;
  // Do 64-bit endianness conversion.
  info.endianness = 0;
  info.flags = CANVAS_FLAGS_READ;
  status =
      amlogic_canvas_config(&canvas_, watermark_input_vmo_.get(), 0, &info, &wm_input_canvas_id_);

  // Allocate a vmo to hold the blended watermark id, then allocate a canvas id for the same.
  status = zx::vmo::create_contiguous(bti, vmo_size, 0, &watermark_blended_vmo_);
  if (status != ZX_OK) {
    FX_LOG(ERROR, TAG, "Unable to get create contiguous blended watermark VMO");
    return status;
  }

  info.flags |= CANVAS_FLAGS_WRITE;
  status = amlogic_canvas_config(&canvas_, watermark_blended_vmo_.get(), 0, &info,
                                 &wm_blended_canvas_id_);
  if (status != ZX_OK) {
    FX_LOG(ERROR, TAG, "Vmo creation for blended watermark image Failed");
    amlogic_canvas_free(&canvas_, wm_input_canvas_id_);
  }
  return status;
}
}  // namespace ge2d
