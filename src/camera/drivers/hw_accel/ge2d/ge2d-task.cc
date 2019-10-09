// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ge2d-task.h"

#include <lib/syslog/global.h>
#include <stdint.h>
#include <zircon/types.h>

#include <memory>

#include <ddk/debug.h>
#include <fbl/alloc_checker.h>

namespace ge2d {

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

  return status;
}

zx_status_t Ge2dTask::InitResize(const buffer_collection_info_2_t* input_buffer_collection,
                                 const buffer_collection_info_2_t* output_buffer_collection,
                                 const resize_info_t* info,
                                 const image_format_2_t* input_image_format,
                                 const image_format_2_t* output_image_format_table_list,
                                 size_t output_image_format_table_count,
                                 uint32_t output_image_format_index,
                                 const hw_accel_callback_t* callback, const zx::bti& bti) {
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
                                    const hw_accel_callback_t* callback, const zx::bti& bti) {
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
  if (status != ZX_OK)
    return status;

  return status;
}
}  // namespace ge2d
