// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_CAMERA_DRIVERS_HW_ACCEL_GDC_GDC_TASK_H_
#define SRC_CAMERA_DRIVERS_HW_ACCEL_GDC_GDC_TASK_H_

#include "../task/task.h"

namespace gdc {
class GdcTask : public generictask::GenericTask {
 public:
  // Returns the physical address for the config VMO.
  zx_paddr_t GetConfigVmoPhysAddr(uint32_t output_format_index) const {
    return pinned_config_vmos_[output_format_index].region(0).phys_addr;
  }

  // Returns the physical address for the config VMO.
  uint64_t GetConfigVmoPhysSize(uint32_t output_format_index) const {
    return pinned_config_vmos_[output_format_index].region(0).size;
  }

  // Static function to create a task object.
  // |input_buffer_collection|              : Input buffer collection.
  // |output_buffer_collection|             : Output buffer collection.
  // [input_image_format]                   : Input image format.
  // [output_image_format]                  : Output image format.
  // |config_vmo_list|                      : Array of configurations.
  // |config_vmo_count|                     : Number of config vmos.
  // |callback|                             : Callback function to call for this task.
  zx_status_t Init(const buffer_collection_info_2_t* input_buffer_collection,
                   const buffer_collection_info_2_t* output_buffer_collection,
                   const image_format_2_t* input_image_format,
                   const image_format_2_t* output_image_format_table_list,
                   size_t output_image_format_table_count, uint32_t output_image_format_index,
                   const zx_handle_t* config_vmo_list, size_t config_vmo_count,
                   const hw_accel_callback_t* callback, const zx::bti& bti);

 private:
  fbl::Array<fzl::PinnedVmo> pinned_config_vmos_;
  zx_status_t PinConfigVmos(const zx_handle_t* config_vmo_list, size_t config_vmo_count,
                            const zx::bti& bti);
};
}  // namespace gdc

#endif  // SRC_CAMERA_DRIVERS_HW_ACCEL_GDC_GDC_TASK_H_
