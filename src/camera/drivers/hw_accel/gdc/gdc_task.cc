// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/camera/drivers/hw_accel/gdc/gdc_task.h"

#include <lib/ddk/debug.h>
#include <lib/syslog/cpp/macros.h>
#include <stdint.h>
#include <zircon/types.h>

#include <memory>

constexpr auto kTag = "gdc";

namespace gdc {

zx_status_t GdcTask::PinConfigVmos(const gdc_config_info* config_vmo_list, size_t config_vmos_count,
                                   std::stack<zx::vmo>& gdc_config_contig_vmos,
                                   const zx::bti& bti) {
  pinned_config_vmos_ =
      fbl::Array<fzl::PinnedVmo>(new fzl::PinnedVmo[config_vmos_count], config_vmos_count);

  for (uint32_t i = 0; i < config_vmos_count; i++) {
    zx::vmo vmo(config_vmo_list[i].config_vmo);
    if (!vmo.is_valid()) {
      return ZX_ERR_INVALID_ARGS;
    }

    uint64_t size;
    auto status = vmo.get_size(&size);
    if (status != ZX_OK) {
      FX_LOGST(ERROR, kTag) << "Unable to get VMO size";
      return status;
    }

    // Attempt to pop a contiguous VMO from the GDC config VMO stack.
    zx::vmo gdc_config_contig_vmo;
    if (!gdc_config_contig_vmos.empty()) {
      gdc_config_contig_vmo.reset(gdc_config_contig_vmos.top().release());
      gdc_config_contig_vmos.pop();
    }

    // Initialize a contiguous VMO for use in this task. If gdc_config_contig_vmo is valid and has
    // the expected size, it will use that. Otherwise, it will fallback to creating a new
    // congiguous VMO.
    zx::vmo contig_vmo;
    status = InitContiguousConfigVmo(gdc_config_contig_vmo, size, bti, contig_vmo);
    if (status != ZX_OK) {
      FX_LOGST(ERROR, kTag) << "Unable to get create contiguous VMO";
      return status;
    }

    // Track the original GDC config VMO in vector. GdcDevice will retrieve these VMOs for reuse
    // when this task ends.
    gdc_owned_config_vmos_.push_back(std::move(gdc_config_contig_vmo));

    fzl::VmoMapper mapped_buffer_vmo;
    status = mapped_buffer_vmo.Map(vmo, 0, 0, ZX_VM_PERM_READ);
    if (status != ZX_OK) {
      FX_LOGST(ERROR, kTag) << "Unable to get map VMO";
      return status;
    }

    fzl::VmoMapper mapped_buffer_contig_vmo;
    status = mapped_buffer_contig_vmo.Map(contig_vmo, 0, 0, ZX_VM_PERM_READ | ZX_VM_PERM_WRITE);
    if (status != ZX_OK) {
      FX_LOGST(ERROR, kTag) << "Unable to get map contig VMO";
      return status;
    }

    memcpy(mapped_buffer_contig_vmo.start(), mapped_buffer_vmo.start(), size);

    // Clean and invalidate the contiguous VMO.
    status = contig_vmo.op_range(ZX_VMO_OP_CACHE_CLEAN, 0, size, nullptr, 0);
    if (status != ZX_OK) {
      FX_LOGST(ERROR, kTag) << "Unable to clean and invalidate the cache";
      return status;
    }

    status = pinned_config_vmos_[i].Pin(contig_vmo, bti, ZX_BTI_CONTIGUOUS | ZX_VM_PERM_READ);
    if (status != ZX_OK) {
      FX_LOGST(ERROR, kTag) << "Failed to pin config VMO";
      return status;
    }
    if (pinned_config_vmos_[i].region_count() != 1) {
      FX_LOGST(ERROR, kTag) << "Buffer is not contiguous";
      return ZX_ERR_NO_MEMORY;
    }

    gdc_config_info config_info;
    config_info.config_vmo = std::move(contig_vmo.release());
    config_info.size = config_vmo_list[i].size;
    config_contig_vmos_.push_back(std::move(config_info));

    // Release the vmos so that the handle doesn't get closed
    __UNUSED zx_handle_t handle = vmo.release();
  }
  return ZX_OK;
}

// static
zx_status_t GdcTask::InitContiguousConfigVmo(zx::vmo& contiguous_config_vmo, size_t size,
                                             const zx::bti& bti, zx::vmo& result) {
  // Check that the passed-in contiguous config VMO exists and is the expected size. If it is,
  // use it.
  zx_status_t status = ZX_OK;
  if (contiguous_config_vmo.is_valid()) {
    uint64_t contiguous_config_vmo_size = 0;
    status = contiguous_config_vmo.get_size(&contiguous_config_vmo_size);
    if (status != ZX_OK) {
      FX_LOGST(ERROR, kTag) << "Failed to get size of GDC config VMO";
      return status;
    }
    if (contiguous_config_vmo_size >= size) {
      status = contiguous_config_vmo.duplicate(ZX_RIGHT_SAME_RIGHTS, &result);
      if (status == ZX_OK) {
        FX_LOGST(DEBUG, kTag) << "Reusing contiguous GDC config VMO";
        return ZX_OK;
      }
    }
  }

  // Fallback: the passed-in VMO was either invalid or not large enough, so create a new contiguous
  // memory VMO for the result.
  FX_LOGST(WARNING, kTag) << "Fallback: creating contiguous GDC config VMO";
  status = zx::vmo::create_contiguous(bti, size, 0, &result);
  if (status != ZX_OK) {
    FX_LOGST(ERROR, kTag) << "Unable to get create contiguous GDC config VMO";
    return status;
  }

  // After creating the fallback contiguous memory VMO, update the passed-in VMO to point to it
  // so that it can be reused the next time GDC configs are initialized.
  status = result.duplicate(ZX_RIGHT_SAME_RIGHTS, &contiguous_config_vmo);
  if (status != ZX_OK) {
    FX_LOGST(WARNING, kTag)
        << "Unable to duplicate newly created contiguous GDC config VMO for reuse";
    return status;
  }

  return ZX_OK;
}

zx_status_t GdcTask::Init(const buffer_collection_info_2_t* input_buffer_collection,
                          const buffer_collection_info_2_t* output_buffer_collection,
                          const image_format_2_t* input_image_format,
                          const image_format_2_t* output_image_format_table_list,
                          size_t output_image_format_table_count,
                          uint32_t output_image_format_index,
                          const gdc_config_info* config_vmo_list, size_t config_vmos_count,
                          std::stack<zx::vmo>& gdc_config_contig_vmos,
                          const hw_accel_frame_callback_t* frame_callback,
                          const hw_accel_res_change_callback_t* res_callback,
                          const hw_accel_remove_task_callback_t* remove_task_callback,
                          const zx::bti& bti) {
  if (frame_callback == nullptr || res_callback == nullptr || config_vmo_list == nullptr ||
      remove_task_callback == nullptr || config_vmos_count == 0 ||
      config_vmos_count != output_image_format_table_count ||
      (output_image_format_table_count < 1) ||
      (output_image_format_index >= output_image_format_table_count)) {
    return ZX_ERR_INVALID_ARGS;
  }

  zx_status_t status =
      PinConfigVmos(config_vmo_list, config_vmos_count, gdc_config_contig_vmos, bti);
  if (status != ZX_OK) {
    FX_LOGST(ERROR, kTag) << "PinConfigVmo Failed";
    return status;
  }

  status = InitBuffers(input_buffer_collection, output_buffer_collection, "GDC", input_image_format,
                       1, 0, output_image_format_table_list, output_image_format_table_count,
                       output_image_format_index, bti, frame_callback, res_callback,
                       remove_task_callback);
  if (status != ZX_OK) {
    FX_LOGST(ERROR, kTag) << "InitBuffers Failed";
    return status;
  }

  return status;
}

void GdcTask::OnRemoveTask(std::stack<zx::vmo>& vmos) {
  for (auto& vmo : gdc_owned_config_vmos_) {
    vmos.push(std::move(vmo));
  }
  gdc_owned_config_vmos_.clear();
}
}  // namespace gdc
