// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "gdc-task.h"

#include <lib/syslog/global.h>
#include <stdint.h>
#include <zircon/types.h>

#include <memory>

#include <ddk/debug.h>
#include <fbl/alloc_checker.h>

namespace gdc {

zx_status_t GdcTask::PinConfigVmo(const zx::vmo& config_vmo, const zx::bti& bti) {
  // Pin the Config VMO.
  zx_status_t status = config_vmo_pinned_.Pin(config_vmo, bti, ZX_BTI_CONTIGUOUS | ZX_VM_PERM_READ);
  if (status != ZX_OK) {
    FX_LOG(ERROR, "%s: Failed to pin config VMO\n", __func__);
    return status;
  }
  if (config_vmo_pinned_.region_count() != 1) {
    FX_LOG(ERROR, "%s: buffer is not contiguous", __func__);
    return ZX_ERR_NO_MEMORY;
  }
  return status;
}

zx_status_t GdcTask::Init(const buffer_collection_info_t* input_buffer_collection,
                          const buffer_collection_info_t* output_buffer_collection,
                          const zx::vmo& config_vmo, const hw_accel_callback_t* callback,
                          const zx::bti& bti) {
  if (callback == nullptr) {
    return ZX_ERR_INVALID_ARGS;
  }

  zx_status_t status = PinConfigVmo(config_vmo, bti);
  if (status != ZX_OK)
    return status;

  status = InitBuffers(input_buffer_collection, output_buffer_collection, bti, callback);
  if (status != ZX_OK) {
    FX_LOG(ERROR, "%s: InitBuffers Failed\n", __func__);
    return status;
  }

  return status;
}

}  // namespace gdc
