// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "task.h"

#include <lib/syslog/global.h>
#include <stdint.h>
#include <zircon/types.h>

#include <memory>

#include <ddk/debug.h>
#include <fbl/alloc_checker.h>

namespace gdc {

// Validates the buffer collection.
static bool IsBufferCollectionValid(const buffer_collection_info_t* buffer_collection) {
  return !(buffer_collection == nullptr || buffer_collection->buffer_count == 0 ||
           buffer_collection->buffer_count > countof(buffer_collection->vmos) ||
           buffer_collection->format.image.pixel_format.type !=
               fuchsia_sysmem_PixelFormatType_NV12);
}

zx_status_t Task::GetInputBufferPhysAddr(uint32_t input_buffer_index, zx_paddr_t* out) const {
  if (input_buffer_index >= input_buffers_.size() || out == nullptr) {
    return ZX_ERR_INVALID_ARGS;
  }
  *out = input_buffers_[input_buffer_index].region(0).phys_addr;

  return ZX_OK;
}

zx_status_t Task::GetInputBufferPhysSize(uint32_t input_buffer_index, uint64_t* out) const {
  if (input_buffer_index >= input_buffers_.size() || out == nullptr) {
    return ZX_ERR_INVALID_ARGS;
  }
  *out = input_buffers_[input_buffer_index].region(0).size;
  return ZX_OK;
}

zx_status_t Task::InitBuffers(const buffer_collection_info_t* input_buffer_collection,
                              const buffer_collection_info_t* output_buffer_collection,
                              const zx::vmo& config_vmo, const zx::bti& bti) {
  if (!IsBufferCollectionValid(input_buffer_collection) ||
      !IsBufferCollectionValid(output_buffer_collection)) {
    return ZX_ERR_INVALID_ARGS;
  }

  // Initialize the VMOPool and pin the output buffers
  zx::vmo output_vmos[countof(output_buffer_collection->vmos)];
  for (uint32_t i = 0; i < output_buffer_collection->buffer_count; ++i) {
    output_vmos[i] = zx::vmo(output_buffer_collection->vmos[i]);
  }

  zx_status_t status = output_buffers_.Init(output_vmos, output_buffer_collection->buffer_count);
  if (status != ZX_OK) {
    FX_LOG(ERROR, "%s: Unable to Init VmoPool \n", __func__);
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
    FX_LOG(ERROR, "%s: Unable to pin buffers \n", __func__);
    return status;
  }

  // Pin the input buffers.
  fbl::AllocChecker ac;
  input_buffers_ =
      fbl ::Array<fzl::PinnedVmo>(new (&ac) fzl::PinnedVmo[input_buffer_collection->buffer_count],
                                  input_buffer_collection->buffer_count);
  if (!ac.check()) {
    return ZX_ERR_NO_MEMORY;
  }

  for (uint32_t i = 0; i < input_buffer_collection->buffer_count; ++i) {
    zx::vmo vmo(input_buffer_collection->vmos[i]);
    status = input_buffers_[i].Pin(vmo, bti, ZX_BTI_CONTIGUOUS | ZX_VM_PERM_READ);

    // Release the vmos so that the buffer collection could be reused.
    // input_buffer_collection already has the handle so its okay to discard
    // this one.
    __UNUSED zx_handle_t handle = vmo.release();

    if (status != ZX_OK) {
      FX_LOG(ERROR, "%s: Unable to pin buffers \n", __func__);
      return status;
    }
    if (input_buffers_[i].region_count() != 1) {
      FX_LOG(ERROR, "%s: buffer is not contiguous", __func__);
      return ZX_ERR_NO_MEMORY;
    }
  }

  // Pin the Config VMO.
  status = config_vmo_pinned_.Pin(config_vmo, bti, ZX_BTI_CONTIGUOUS | ZX_VM_PERM_READ);
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

// static
zx_status_t Task::Create(const buffer_collection_info_t* input_buffer_collection,
                         const buffer_collection_info_t* output_buffer_collection,
                         const zx::vmo& config_vmo, const gdc_callback_t* callback,
                         const zx::bti& bti, std::unique_ptr<Task>* out) {
  if (callback == nullptr || out == nullptr) {
    return ZX_ERR_INVALID_ARGS;
  }

  fbl::AllocChecker ac;
  auto task = std::unique_ptr<Task>(new (&ac) Task());
  if (!ac.check()) {
    return ZX_ERR_NO_MEMORY;
  }

  zx_status_t status =
      task->InitBuffers(input_buffer_collection, output_buffer_collection, config_vmo, bti);
  if (status != ZX_OK) {
    FX_LOG(ERROR, "%s: InitBuffers Failed\n", __func__);
    return status;
  }

  task->input_format_ = input_buffer_collection->format.image;
  task->output_format_ = output_buffer_collection->format.image;
  task->callback_ = callback;

  *out = std::move(task);
  return status;
}

}  // namespace gdc
