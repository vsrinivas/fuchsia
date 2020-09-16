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
#include <fbl/algorithm.h>

constexpr uint32_t kEndianness = 7;
constexpr auto kTag = "ge2d";

namespace ge2d {
ScopedCanvasId::ScopedCanvasId(ScopedCanvasId&& other) {
  canvas_ = other.canvas_;
  id_ = other.id_;
  other.canvas_ = nullptr;
}
ScopedCanvasId& ScopedCanvasId::operator=(ScopedCanvasId&& other) {
  Reset();
  canvas_ = other.canvas_;
  id_ = other.id_;
  other.canvas_ = nullptr;
  return *this;
}

void ScopedCanvasId::Reset() {
  if (canvas_) {
    amlogic_canvas_free(canvas_, id_);
  }
  canvas_ = nullptr;
  id_ = 0;
}

static zx_status_t CanvasConfig(const amlogic_canvas_protocol_t* canvas, zx_handle_t vmo,
                                uint32_t offset, const canvas_info_t* info,
                                ScopedCanvasId* canvas_id_out) {
  uint8_t id;
  zx_handle_t vmo_dup;
  zx_status_t status = zx_handle_duplicate(vmo, ZX_RIGHT_SAME_RIGHTS, &vmo_dup);
  if (status != ZX_OK) {
    return status;
  }
  status = amlogic_canvas_config(canvas, vmo_dup, offset, info, &id);
  if (status != ZX_OK) {
    return status;
  }
  *canvas_id_out = ScopedCanvasId(canvas, id);
  return ZX_OK;
}

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

  status = CanvasConfig(&canvas_, vmo_in, 0,  // offset of plane 0 is at 0.
                        &info, &canvas_ids.canvas_idx[kYComponent]);
  if (status != ZX_OK) {
    return status;
  }

  if (image_format->pixel_format.type != fuchsia_sysmem_PixelFormatType_NV12) {
    canvas_ids.canvas_idx[kUVComponent] = ScopedCanvasId();
    return ZX_OK;
  }

  info.height /= 2;  // For NV12, second plane height is 1/2 first.
  status =
      CanvasConfig(&canvas_, vmo_in, image_format->display_height * image_format->bytes_per_row,
                   &info, &canvas_ids.canvas_idx[kUVComponent]);
  if (status != ZX_OK) {
    return ZX_ERR_NO_RESOURCES;
  }
  return ZX_OK;
}

zx_status_t Ge2dTask::AllocInputCanvasIds(const buffer_collection_info_2_t* input_buffer_collection,
                                          const image_format_2_t* input_image_format,
                                          bool enable_write) {
  if (input_image_format->pixel_format.type != fuchsia_sysmem_PixelFormatType_NV12 &&
      input_image_format->pixel_format.type != fuchsia_sysmem_PixelFormatType_R8G8B8A8) {
    return ZX_ERR_NOT_SUPPORTED;
  }
  if (((input_image_format->display_height % 2) != 0) || (input_image_format->bytes_per_row == 0)) {
    return ZX_ERR_INVALID_ARGS;
  }
  num_input_canvas_ids_ = 0;

  auto input_image_canvas_ids =
      std::make_unique<input_image_canvas_id_t[]>(input_buffer_collection->buffer_count);
  for (uint32_t i = 0; i < input_buffer_collection->buffer_count; i++) {
    uint32_t flags = CANVAS_FLAGS_READ;
    if (enable_write)
      flags |= CANVAS_FLAGS_WRITE;
    zx_status_t status = AllocCanvasId(input_image_format, input_buffer_collection->buffers[i].vmo,
                                       input_image_canvas_ids[i].canvas_ids, flags);
    if (status != ZX_OK) {
      return status;
    }
    // Canvas id allocation was successful. Dup the vmo handle and save it along with
    // the canvas ids. We need the vmo handle when we change the input resolution.
    zx_handle_t vmo_dup;
    status = zx_handle_duplicate(input_buffer_collection->buffers[i].vmo, ZX_RIGHT_SAME_RIGHTS,
                                 &vmo_dup);
    if (status != ZX_OK) {
      return status;
    }
    input_image_canvas_ids[i].vmo = zx::vmo(vmo_dup);
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
  if (output_image_format->pixel_format.type != fuchsia_sysmem_PixelFormatType_NV12 &&
      output_image_format->pixel_format.type != fuchsia_sysmem_PixelFormatType_R8G8B8A8) {
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

  auto buf_canvas_ids =
      std::make_unique<buf_canvas_ids_t[]>(output_buffer_collection->buffer_count);
  for (uint32_t i = 0; i < output_buffer_collection->buffer_count; i++) {
    buf_canvas_ids[i].output_buffer = *WriteLockOutputBuffer();
    zx_handle_t vmo_handle = buf_canvas_ids[i].output_buffer.vmo_handle();
    zx_status_t status =
        AllocCanvasId(output_image_format, vmo_handle, buf_canvas_ids[i].canvas_ids,
                      CANVAS_FLAGS_READ | CANVAS_FLAGS_WRITE);
    if (status != ZX_OK) {
      for (uint32_t j = 0; j < i; j++) {
        ReleaseOutputBuffer(std::move(buf_canvas_ids[j].output_buffer));
      }
      return status;
    }
  }
  for (uint32_t i = 0; i < output_buffer_collection->buffer_count; i++) {
    buffer_map_[buf_canvas_ids[i].output_buffer.vmo_handle()] =
        std::move(buf_canvas_ids[i].canvas_ids);
    ReleaseOutputBuffer(std::move(buf_canvas_ids[i].output_buffer));
  }
  return ZX_OK;
}

zx_status_t Ge2dTask::AllocCanvasIds(const buffer_collection_info_2_t* input_buffer_collection,
                                     const buffer_collection_info_2_t* output_buffer_collection,
                                     const image_format_2_t* input_image_format,
                                     const image_format_2_t* output_image_format) {
  zx_status_t status = AllocInputCanvasIds(input_buffer_collection, input_image_format,
                                           /*enable_write=*/!output_buffer_collection);
  if (status != ZX_OK) {
    return status;
  }
  if (output_buffer_collection) {
    status = AllocOutputCanvasIds(output_buffer_collection, output_image_format);
  }
  return status;
}

void Ge2dTask::FreeCanvasIds() {
  for (uint32_t j = 0; j < num_input_canvas_ids_; j++) {
    input_image_canvas_ids_[j].canvas_ids.canvas_idx[kYComponent] = ScopedCanvasId();
    input_image_canvas_ids_[j].canvas_ids.canvas_idx[kUVComponent] = ScopedCanvasId();
    input_image_canvas_ids_[j].vmo.reset();
  }
  num_input_canvas_ids_ = 0;
  for (auto it = buffer_map_.begin(); it != buffer_map_.end(); ++it) {
    it->second.canvas_idx[kYComponent] = ScopedCanvasId();
    it->second.canvas_idx[kUVComponent] = ScopedCanvasId();
  }
  for (auto& watermark : wm_) {
    watermark.input_canvas_id.canvas_idx[0] = ScopedCanvasId();
  }
  wm_blended_canvas_id_.canvas_idx[0] = ScopedCanvasId();
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
    it.second = std::move(canvas_ids);
  }
}

void Ge2dTask::AllocateWatermarkCanvasIds() {
  for (auto& wm : wm_) {
    wm.input_canvas_id = image_canvas_id{};
  }
  if (input_format_index() < wm_.size()) {
    auto& wm = wm_[input_format_index()];
    image_canvas_id_t canvas_ids;
    zx_status_t status = AllocCanvasId(&wm.image_format, wm.watermark_input_vmo.get(), canvas_ids,
                                       CANVAS_FLAGS_READ);
    ZX_ASSERT(status == ZX_OK);
    wm.input_canvas_id = std::move(canvas_ids);

    status = AllocCanvasId(&wm.image_format, watermark_blended_vmo_.get(), canvas_ids,
                           CANVAS_FLAGS_READ | CANVAS_FLAGS_WRITE);
    ZX_ASSERT(status == ZX_OK);
    wm_blended_canvas_id_ = std::move(canvas_ids);
  }
}

void Ge2dTask::Ge2dChangeInputRes(uint32_t new_input_buffer_index) {
  set_input_format_index(new_input_buffer_index);
  // Re-allocate the Input canvas IDs.
  image_format_2_t format = input_format();
  for (uint32_t j = 0; j < num_input_canvas_ids_; j++) {
    image_canvas_id_t canvas_ids;
    zx_status_t status = AllocCanvasId(&format, input_image_canvas_ids_[j].vmo.get(), canvas_ids,
                                       CANVAS_FLAGS_READ | CANVAS_FLAGS_WRITE);
    ZX_ASSERT(status == ZX_OK);
    input_image_canvas_ids_[j].canvas_ids = std::move(canvas_ids);
  }
  AllocateWatermarkCanvasIds();
}

zx_status_t Ge2dTask::Init(const buffer_collection_info_2_t* input_buffer_collection,
                           const buffer_collection_info_2_t* output_buffer_collection,
                           const image_format_2_t* input_image_format_table_list,
                           size_t input_image_format_table_count, uint32_t input_image_format_index,
                           const image_format_2_t* output_image_format_table_list,
                           size_t output_image_format_table_count,
                           uint32_t output_image_format_index,
                           const hw_accel_frame_callback_t* frame_callback,
                           const hw_accel_res_change_callback_t* res_callback,
                           const hw_accel_remove_task_callback_t* remove_task_callback,
                           const zx::bti& bti) {
  if ((output_image_format_table_count < 1) ||
      (output_image_format_index >= output_image_format_table_count) ||
      (input_image_format_table_count < 1) ||
      (input_image_format_index >= input_image_format_table_count) || (frame_callback == nullptr) ||
      (res_callback == nullptr) || remove_task_callback == nullptr) {
    return ZX_ERR_INVALID_ARGS;
  }

  zx_status_t status;
  if (output_buffer_collection) {
    status = InitBuffers(input_buffer_collection, output_buffer_collection,
                         input_image_format_table_list, input_image_format_table_count,
                         input_image_format_index, output_image_format_table_list,
                         output_image_format_table_count, output_image_format_index, bti,
                         frame_callback, res_callback, remove_task_callback);
  } else {
    status = InitInputBuffers(input_buffer_collection, input_image_format_table_list,
                              input_image_format_table_count, input_image_format_index, bti,
                              frame_callback, res_callback, remove_task_callback);
  }
  if (status != ZX_OK) {
    FX_LOG(ERROR, kTag, "InitBuffers Failed");
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
                                 const hw_accel_remove_task_callback_t* remove_task_callback,
                                 const zx::bti& bti, amlogic_canvas_protocol_t canvas) {
  canvas_ = canvas;

  zx_status_t status =
      Init(input_buffer_collection, output_buffer_collection, input_image_format, 1, 0,
           output_image_format_table_list, output_image_format_table_count,
           output_image_format_index, frame_callback, res_callback, remove_task_callback, bti);
  if (status != ZX_OK) {
    FX_LOG(ERROR, kTag, "Init Failed");
    return status;
  }

  // Make a copy of the resize info
  res_info_ = *info;

  task_type_ = GE2D_RESIZE;

  return status;
}

zx_status_t Ge2dTask::InitializeWatermarkImages(const water_mark_info_t* wm_info,
                                                size_t image_format_table_count, const zx::bti& bti,
                                                amlogic_canvas_protocol_t canvas) {
  size_t max_size = 0;
  zx_status_t status;
  for (uint32_t i = 0; i < image_format_table_count; i++) {
    wm_.push_back({});
    auto& wm = wm_.back();
    if (wm_info[i].wm_image_format.pixel_format.type != fuchsia_sysmem_PixelFormatType_R8G8B8A8) {
      FX_LOG(ERROR, kTag, "Image format type not supported");
      return ZX_ERR_NOT_SUPPORTED;
    }

    // Make copy of watermark info, pin watermark vmo.
    wm.loc_x = wm_info[i].loc_x;
    wm.loc_y = wm_info[i].loc_y;
    wm.image_format = wm_info[i].wm_image_format;
    constexpr uint32_t kCanvasMinAlignment = 32;
    wm.image_format.bytes_per_row =
        fbl::round_up(wm.image_format.bytes_per_row, kCanvasMinAlignment);

    uint64_t input_vmo_size =
        wm.image_format.display_height * wm_info[i].wm_image_format.bytes_per_row;
    uint64_t output_vmo_size = wm.image_format.display_height * wm.image_format.bytes_per_row;
    uint64_t rounded_input_vmo_size = fbl::round_up(input_vmo_size, ZX_PAGE_SIZE);
    max_size = std::max(output_vmo_size, max_size);

    // Round the width up to be a multiple of 2, or otherwise the final RGBA to NV12
    // blit hangs.
    if ((wm.image_format.display_width % 2) != 0) {
      wm.image_format.display_width++;
      wm.image_format.coded_width++;
      // bytes_per_row must be a multiple of 32, so this rounding up should
      // work.
      ZX_ASSERT(wm.image_format.coded_width * 4 <= wm.image_format.bytes_per_row);
    }

    // The watermark vmo may not necessarily be contig. Allocate a contig vmo and
    // copy the contents of the watermark image into it and use that.
    status = zx::vmo::create_contiguous(bti, output_vmo_size, 0, &wm.watermark_input_vmo);
    if (status != ZX_OK) {
      FX_LOG(ERROR, kTag, "Unable to get create contiguous input watermark VMO");
      return status;
    }
    // Copy the watermark image over.
    fzl::VmoMapper mapped_watermark_input_vmo;
    status = mapped_watermark_input_vmo.Map(*zx::unowned_vmo(wm_info[i].watermark_vmo), 0,
                                            rounded_input_vmo_size, ZX_VM_PERM_READ);
    if (status != ZX_OK) {
      FX_LOG(ERROR, kTag, "Unable to get map for watermark input VMO");
      return status;
    }
    fzl::VmoMapper mapped_contig_vmo;
    status =
        mapped_contig_vmo.Map(wm.watermark_input_vmo, 0, 0, ZX_VM_PERM_READ | ZX_VM_PERM_WRITE);
    if (status != ZX_OK) {
      FX_LOG(ERROR, kTag, "Unable to get map contig watermark VMO");
      return status;
    }

    // Expand out to ensure bytes_per_row is a multiple of 32, as that's what the canvas requires.
    for (uint32_t y = 0; y < wm.image_format.display_height; ++y) {
      auto output_row =
          static_cast<uint8_t*>(mapped_contig_vmo.start()) + wm.image_format.bytes_per_row * y;
      memcpy(output_row,
             static_cast<uint8_t*>(mapped_watermark_input_vmo.start()) +
                 wm_info[i].wm_image_format.bytes_per_row * y,
             wm_info[i].wm_image_format.bytes_per_row);
      // Implement global alpha on the CPU.
      if (wm_info[i].global_alpha != 1.0f) {
        for (uint32_t x = 0; x < wm.image_format.coded_width; ++x) {
          uint8_t* alpha = &output_row[4 * x + 3];
          *alpha = *alpha * wm_info[i].global_alpha;
        }
      }
    }

    zx_cache_flush(mapped_contig_vmo.start(), output_vmo_size, ZX_CACHE_FLUSH_DATA);
  }
  // Allocate a vmo to hold the blended watermark id, then allocate a canvas id for the same.
  status = zx::vmo::create_contiguous(bti, max_size, 0, &watermark_blended_vmo_);
  if (status != ZX_OK) {
    FX_LOG(ERROR, kTag, "Unable to get create contiguous blended watermark VMO");
    return status;
  }
  watermark_blended_vmo_.op_range(ZX_VMO_OP_CACHE_CLEAN, 0, max_size, nullptr, 0);
  AllocateWatermarkCanvasIds();
  return ZX_OK;
}

zx_status_t Ge2dTask::InitWatermark(const buffer_collection_info_2_t* input_buffer_collection,
                                    const buffer_collection_info_2_t* output_buffer_collection,
                                    const water_mark_info_t* wm_info,
                                    const image_format_2_t* image_format_table_list,
                                    size_t image_format_table_count, uint32_t image_format_index,
                                    const hw_accel_frame_callback_t* frame_callback,
                                    const hw_accel_res_change_callback_t* res_callback,
                                    const hw_accel_remove_task_callback_t* remove_task_callback,
                                    const zx::bti& bti, amlogic_canvas_protocol_t canvas) {
  canvas_ = canvas;

  zx_status_t status = Init(input_buffer_collection, output_buffer_collection,
                            image_format_table_list, image_format_table_count, image_format_index,
                            image_format_table_list, image_format_table_count, image_format_index,
                            frame_callback, res_callback, remove_task_callback, bti);
  if (status != ZX_OK) {
    FX_LOG(ERROR, kTag, "Init Failed");
    return status;
  }
  task_type_ = GE2D_WATERMARK;

  return InitializeWatermarkImages(wm_info, image_format_table_count, bti, canvas);
}

zx_status_t Ge2dTask::InitInPlaceWatermark(
    const buffer_collection_info_2_t* buffer_collection, const water_mark_info_t* wm_info,
    const image_format_2_t* image_format_table_list, size_t image_format_table_count,
    uint32_t image_format_index, const hw_accel_frame_callback_t* frame_callback,
    const hw_accel_res_change_callback_t* res_callback,
    const hw_accel_remove_task_callback_t* remove_task_callback, const zx::bti& bti,
    amlogic_canvas_protocol_t canvas) {
  canvas_ = canvas;

  zx_status_t status =
      Init(buffer_collection, nullptr, image_format_table_list, image_format_table_count,
           image_format_index, image_format_table_list, image_format_table_count,
           image_format_index, frame_callback, res_callback, remove_task_callback, bti);
  if (status != ZX_OK) {
    FX_LOG(ERROR, kTag, "Init Failed");
    return status;
  }
  task_type_ = GE2D_IN_PLACE_WATERMARK;

  return InitializeWatermarkImages(wm_info, image_format_table_count, bti, canvas);
}
}  // namespace ge2d
