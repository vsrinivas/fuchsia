// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_CAMERA_DRIVERS_CONTROLLER_MEMORY_ALLOCATION_H_
#define SRC_CAMERA_DRIVERS_CONTROLLER_MEMORY_ALLOCATION_H_

#include <fuchsia/sysmem/cpp/fidl.h>

namespace camera {

class ControllerMemoryAllocator {
 public:
  explicit ControllerMemoryAllocator(fuchsia::sysmem::AllocatorSyncPtr sysmem_allocator)
      : sysmem_allocator_(std::move(sysmem_allocator)){};

  // Takes in two sets of constraints and allocated memory using sysmem based on those two sets of
  // constraints.
  zx_status_t AllocateSharedMemory(
      const std::vector<fuchsia::sysmem::BufferCollectionConstraints>& constraints,
      fuchsia::sysmem::BufferCollectionInfo_2* out_buffer_collection_info) const;

 private:
  fuchsia::sysmem::AllocatorSyncPtr sysmem_allocator_;
};

}  // namespace camera

#endif  // SRC_CAMERA_DRIVERS_CONTROLLER_MEMORY_ALLOCATION_H_
