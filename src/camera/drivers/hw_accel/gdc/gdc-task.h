// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_CAMERA_DRIVERS_HW_ACCEL_GDC_GDC_TASK_H_
#define SRC_CAMERA_DRIVERS_HW_ACCEL_GDC_GDC_TASK_H_

#include <vector>

#include <ddktl/protocol/gdc.h>
#include <fbl/array.h>

#include "src/camera/drivers/hw_accel/task/task.h"

namespace gdc {
class GdcTask : public generictask::GenericTask {
 public:
  ~GdcTask() {
    for (auto config : config_contig_vmos_) {
      ZX_ASSERT(ZX_OK == zx_handle_close(config.config_vmo));
    }
  }
  // Returns the physical address for the config VMO.
  zx_paddr_t GetConfigVmoPhysAddr(uint32_t output_format_index) const {
    return pinned_config_vmos_[output_format_index].region(0).phys_addr;
  }

  // Returns the VMO size for the config VMO.
  uint64_t GetConfigVmoSize(uint32_t output_format_index) const {
    return config_contig_vmos_[output_format_index].size;
  }

  // Static function to create a task object.
  // |input_buffer_collection|              : Input buffer collection.
  // |output_buffer_collection|             : Output buffer collection.
  // [input_image_format]                   : Input image format.
  // [output_image_format]                  : Output image format.
  // |config_vmo_list|                      : Array of configurations info.
  // |config_vmo_count|                     : Number of config vmos.
  // |frame_callback|                       : Process frame callback.
  // |remove_task_callback|                 : Remove task callback.
  // |res_callback|                         : Res change callback.
  zx_status_t Init(const buffer_collection_info_2_t* input_buffer_collection,
                   const buffer_collection_info_2_t* output_buffer_collection,
                   const image_format_2_t* input_image_format,
                   const image_format_2_t* output_image_format_table_list,
                   size_t output_image_format_table_count, uint32_t output_image_format_index,
                   const gdc_config_info* config_vmo_list, size_t config_vmos_count,
                   const hw_accel_frame_callback_t* frame_callback,
                   const hw_accel_res_change_callback* res_callback,
                   const hw_accel_remove_task_callback_t* remove_task_callback, const zx::bti& bti);

 private:
  zx_status_t PinConfigVmos(const gdc_config_info* config_vmo_list, size_t config_vmos_count,
                            const zx::bti& bti);

  fbl::Array<fzl::PinnedVmo> pinned_config_vmos_;
  std::vector<gdc_config_info> config_contig_vmos_;
};
}  // namespace gdc

#endif  // SRC_CAMERA_DRIVERS_HW_ACCEL_GDC_GDC_TASK_H_
