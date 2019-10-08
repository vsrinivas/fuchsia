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
  zx_paddr_t GetConigVmoPhysAddr() const { return config_vmo_pinned_.region(0).phys_addr; }

  // Returns the physical address for the config VMO.
  uint64_t GetConigVmoPhysSize() const { return config_vmo_pinned_.region(0).size; }

  // Static function to create a task object.
  // |input_buffer_collection|              : Input buffer collection.
  // |output_buffer_collection|             : Output buffer collection.
  // [input_image_format]                   : Input image format.
  // [output_image_format]                  : Output image format.
  // |config_vmo|                           : Configuration is stored in this
  // VMO. |callback|                        : Callback function to call for
  // this task. |out|                       : Pointer to a task
  // object returned to the caller.
  zx_status_t Init(const buffer_collection_info_2_t* input_buffer_collection,
                   const buffer_collection_info_2_t* output_buffer_collection,
                   const image_format_2_t* input_image_format,
                   const image_format_2_t* output_image_format, const zx::vmo& config_vmo,
                   const hw_accel_callback_t* callback, const zx::bti& bti);

 private:
  fzl::PinnedVmo config_vmo_pinned_;
  zx_status_t PinConfigVmo(const zx::vmo& config_vmo, const zx::bti& bti);
};
}  // namespace gdc

#endif  // SRC_CAMERA_DRIVERS_HW_ACCEL_GDC_GDC_TASK_H_
