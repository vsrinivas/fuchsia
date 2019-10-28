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

static constexpr uint32_t kEndianness = 7;

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
  status = canvas_.ops->config(canvas_.ctx, vmo_dup,
                               0,  // offset of plane 0 is at 0.
                               &info, &canvas_ids.canvas_idx[kYComponent]);
  info.height /= 2;  // For NV12, second plane height is 1/2 first.
  status = canvas_.ops->config(canvas_.ctx, vmo_in,
                               image_format->display_height * image_format->bytes_per_row, &info,
                               &canvas_ids.canvas_idx[kUVComponent]);
  if (status != ZX_OK) {
    canvas_.ops->free(canvas_.ctx, canvas_ids.canvas_idx[kYComponent]);
    return ZX_ERR_NO_RESOURCES;
  }
  return ZX_OK;
}

zx_status_t Ge2dTask::AllocInputCanvasIds(const buffer_collection_info_2_t* input_buffer_collection,
                                          const image_format_2_t* input_image_format) {
  if (input_image_format->pixel_format.type != ZX_PIXEL_FORMAT_NV12) {
    return ZX_ERR_NOT_SUPPORTED;
  }
  if (((input_image_format->display_height % 2) != 0) || (input_image_format->bytes_per_row == 0)) {
    return ZX_ERR_INVALID_ARGS;
  }
  num_input_canvas_ids_ = 0;
  fbl::AllocChecker ac;
  std::unique_ptr<image_canvas_id_t[]> canvas_ids;
  canvas_ids = std::unique_ptr<image_canvas_id_t[]>(
      new (&ac) image_canvas_id_t[input_buffer_collection->buffer_count]);
  if (!ac.check()) {
    return ZX_ERR_NO_MEMORY;
  }
  zx_status_t status;
  for (uint32_t i = 0; i < input_buffer_collection->buffer_count; i++) {
    status = AllocCanvasId(input_image_format, input_buffer_collection->buffers[i].vmo,
                           canvas_ids[i], CANVAS_FLAGS_READ);
    if (status != ZX_OK) {
      return status;
    }
  }
  num_input_canvas_ids_ = input_buffer_collection->buffer_count;
  input_image_canvas_ids_ = move(canvas_ids);
  return ZX_OK;
}

// Allocation of output buffer canvas ids is a bit more involved. We need to
// allocate the canvas ids and then insert them in a hashmap, where we can look
// up by the vmo (handle) the underlying buffer.
zx_status_t Ge2dTask::AllocOutputCanvasIds(
    const buffer_collection_info_2_t* output_buffer_collection,
    const image_format_2_t* output_image_format) {
  if (output_image_format->pixel_format.type != ZX_PIXEL_FORMAT_NV12) {
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
        canvas_.ops->free(canvas_.ctx, buf_canvas_ids[j].canvas_ids.canvas_idx[kYComponent]);
        canvas_.ops->free(canvas_.ctx, buf_canvas_ids[j].canvas_ids.canvas_idx[kUVComponent]);
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
    canvas_.ops->free(canvas_.ctx, input_image_canvas_ids_[j].canvas_idx[kYComponent]);
    canvas_.ops->free(canvas_.ctx, input_image_canvas_ids_[j].canvas_idx[kUVComponent]);
  }
  for (auto it = buffer_map_.cbegin(); it != buffer_map_.cend(); ++it) {
    canvas_.ops->free(canvas_.ctx, it->second.canvas_idx[kYComponent]);
    canvas_.ops->free(canvas_.ctx, it->second.canvas_idx[kUVComponent]);
  }
}

zx_status_t Ge2dTask::PinWatermarkVmo(const zx::vmo& watermark_vmo, const zx::bti& bti) {
  // Pin the Watermark VMO.
  zx_status_t status =
      wm_.watermark_vmo_pinned_.Pin(watermark_vmo, bti, ZX_BTI_CONTIGUOUS | ZX_VM_PERM_READ);
  if (status != ZX_OK) {
    FX_LOG(ERROR, "%s: Failed to pin watermark VMO\n", __func__);
    return status;
  }
  if (wm_.watermark_vmo_pinned_.region_count() != 1) {
    FX_LOG(ERROR, "%s: buffer is not contiguous", __func__);
    return ZX_ERR_NO_MEMORY;
  }
  return status;
}

zx_status_t Ge2dTask::Init(const buffer_collection_info_2_t* input_buffer_collection,
                           const buffer_collection_info_2_t* output_buffer_collection,
                           const image_format_2_t* input_image_format,
                           const image_format_2_t* output_image_format_table_list,
                           size_t output_image_format_table_count,
                           uint32_t output_image_format_index, const hw_accel_callback_t* callback,
                           const zx::bti& bti) {
  if ((output_image_format_table_count < 1) ||
      (output_image_format_index >= output_image_format_table_count) || (callback == nullptr)) {
    return ZX_ERR_INVALID_ARGS;
  }

  zx_status_t status =
      InitBuffers(input_buffer_collection, output_buffer_collection, input_image_format,
                  output_image_format_table_list, output_image_format_table_count,
                  output_image_format_index, bti, callback);
  if (status != ZX_OK) {
    FX_LOG(ERROR, "%s: InitBuffers Failed\n", __func__);
    return status;
  }

  status = AllocCanvasIds(input_buffer_collection, output_buffer_collection, input_image_format,
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
                                 const hw_accel_callback_t* callback, const zx::bti& bti,
                                 amlogic_canvas_protocol_t canvas) {
  canvas_ = canvas;

  zx_status_t status = Init(input_buffer_collection, output_buffer_collection, input_image_format,
                            output_image_format_table_list, output_image_format_table_count,
                            output_image_format_index, callback, bti);
  if (status != ZX_OK) {
    FX_LOG(ERROR, "%s: Init Failed\n", __func__);
    return status;
  }

  // Make a copy of the resize info
  res_info_ = *info;

  return status;
}

zx_status_t Ge2dTask::InitWatermark(const buffer_collection_info_2_t* input_buffer_collection,
                                    const buffer_collection_info_2_t* output_buffer_collection,
                                    const water_mark_info_t* info, const zx::vmo& watermark_vmo,
                                    const image_format_2_t* input_image_format,
                                    const image_format_2_t* output_image_format_table_list,
                                    size_t output_image_format_table_count,
                                    uint32_t output_image_format_index,
                                    const hw_accel_callback_t* callback, const zx::bti& bti,
                                    amlogic_canvas_protocol_t canvas) {
  canvas_ = canvas;

  zx_status_t status = Init(input_buffer_collection, output_buffer_collection, input_image_format,
                            output_image_format_table_list, output_image_format_table_count,
                            output_image_format_index, callback, bti);
  if (status != ZX_OK) {
    FX_LOG(ERROR, "%s: Init Failed\n", __func__);
    return status;
  }

  // Make copy of watermark info, pin watermark vmo.
  wm_.loc_x = info->loc_x;
  wm_.loc_y = info->loc_y;
  wm_.wm_image_format = info->wm_image_format;
  status = PinWatermarkVmo(watermark_vmo, bti);

  return status;
}
}  // namespace ge2d
