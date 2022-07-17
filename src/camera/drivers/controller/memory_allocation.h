// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_CAMERA_DRIVERS_CONTROLLER_MEMORY_ALLOCATION_H_
#define SRC_CAMERA_DRIVERS_CONTROLLER_MEMORY_ALLOCATION_H_

#include <fuchsia/hardware/sysmem/cpp/banjo.h>
#include <fuchsia/sysmem/cpp/fidl.h>

namespace camera {

struct BufferCollection {
  fuchsia::sysmem::BufferCollectionPtr ptr;
  fuchsia::sysmem::BufferCollectionInfo_2 buffers;
};

class ControllerMemoryAllocator {
 public:
  explicit ControllerMemoryAllocator(const ddk::SysmemProtocolClient& sysmem);

  // Takes in a set of constraints and allocates memory using sysmem based on those
  // constraints.
  zx_status_t AllocateSharedMemory(
      const std::vector<fuchsia::sysmem::BufferCollectionConstraints>& constraints,
      BufferCollection& out_buffer_collection, const std::string& name) const;

  // Duplicates the provided token, assigns it "default" constraints, and returns the collection.
  fuchsia::sysmem::BufferCollectionHandle AttachObserverCollection(
      fuchsia::sysmem::BufferCollectionTokenHandle& token);

 private:
  const ddk::SysmemProtocolClient& sysmem_;
  fuchsia::sysmem::AllocatorSyncPtr sysmem_allocator_;
};

}  // namespace camera

#endif  // SRC_CAMERA_DRIVERS_CONTROLLER_MEMORY_ALLOCATION_H_
