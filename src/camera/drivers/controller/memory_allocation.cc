// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/camera/drivers/controller/memory_allocation.h"

#include <lib/syslog/global.h>
#include <lib/trace/event.h>
#include <zircon/errors.h>

#include "src/lib/fsl/handles/object_info.h"

namespace camera {

constexpr auto kTag = "camera_controller";

ControllerMemoryAllocator::ControllerMemoryAllocator(
    fuchsia::sysmem::AllocatorSyncPtr sysmem_allocator)
    : sysmem_allocator_(std::move(sysmem_allocator)) {
  if (sysmem_allocator_)
    sysmem_allocator_->SetDebugClientInfo("camera controller " + fsl::GetCurrentProcessName(),
                                          fsl::GetCurrentProcessKoid());
}

zx_status_t ControllerMemoryAllocator::AllocateSharedMemory(
    const std::vector<fuchsia::sysmem::BufferCollectionConstraints>& constraints,
    BufferCollection& out_buffer_collection, std::string name) const {
  TRACE_DURATION("camera", "ControllerMemoryAllocator::AllocateSharedMemory");

  auto num_constraints = constraints.size();

  if (!num_constraints) {
    return ZX_ERR_INVALID_ARGS;
  }

  // Create tokens which we'll hold on to to get our buffer_collection.
  std::vector<fuchsia::sysmem::BufferCollectionTokenSyncPtr> tokens(num_constraints);

  // Start the allocation process.
  auto status = sysmem_allocator_->AllocateSharedCollection(tokens[0].NewRequest());
  if (status != ZX_OK) {
    FX_LOG(ERROR, kTag, "Failed to create token");
    return status;
  }

  // Duplicate the tokens.
  for (uint32_t i = 1; i < num_constraints; i++) {
    status = tokens[0]->Duplicate(std::numeric_limits<uint32_t>::max(), tokens[i].NewRequest());
    if (status != ZX_OK) {
      FX_LOG(ERROR, kTag, "Failed to duplicate token");
      return status;
    }
  }

  // Now convert into a Logical BufferCollection:
  std::vector<fuchsia::sysmem::BufferCollectionSyncPtr> buffer_collections(num_constraints);

  status = sysmem_allocator_->BindSharedCollection(std::move(tokens[0]),
                                                   buffer_collections[0].NewRequest());
  if (status != ZX_OK) {
    FX_LOG(ERROR, kTag, "Failed to create logical buffer collection");
    return status;
  }

  status = buffer_collections[0]->Sync();
  if (status != ZX_OK) {
    FX_LOG(ERROR, kTag, "Failed to sync");
    return status;
  }

  constexpr uint32_t kNamePriority = 10u;
  buffer_collections[0]->SetName(kNamePriority, name);

  // Create rest of the logical buffer collections
  for (uint32_t i = 1; i < num_constraints; i++) {
    status = sysmem_allocator_->BindSharedCollection(std::move(tokens[i]),
                                                     buffer_collections[i].NewRequest());
    if (status != ZX_OK) {
      FX_LOG(ERROR, kTag, "Failed to create logical buffer collection");
      return status;
    }
  }

  // Set constraints
  for (uint32_t i = 0; i < num_constraints; i++) {
    FX_LOGF(DEBUG, kTag, "Allocate %s with constraints %dx%d camp %d min %d max %d", name.c_str(),
            constraints[i].image_format_constraints[0].required_max_coded_width,
            constraints[i].image_format_constraints[0].required_max_coded_height,
            constraints[i].min_buffer_count_for_camping, constraints[i].min_buffer_count,
            constraints[i].max_buffer_count);
    status = buffer_collections[i]->SetConstraints(true, constraints[i]);
    if (status != ZX_OK) {
      FX_LOG(ERROR, kTag, "Failed to set buffer collection constraints");
      return status;
    }
  }

  zx_status_t allocation_status;
  status = buffer_collections[0]->WaitForBuffersAllocated(&allocation_status,
                                                          &out_buffer_collection.buffers);
  if (status != ZX_OK) {
    FX_LOG(ERROR, kTag, "Failed to wait for buffer collection info.");
    return status;
  }
  if (allocation_status != ZX_OK) {
    FX_LOG(ERROR, kTag, "Failed to allocate buffer collection.");
    return allocation_status;
  }

  FX_LOGF(DEBUG, kTag, "Allocated %s count %d size %d", name.c_str(),
          out_buffer_collection.buffers.buffer_count,
          out_buffer_collection.buffers.settings.buffer_settings.size_bytes);

  // Leave first collection handle open to return
  out_buffer_collection.ptr = buffer_collections[0].Unbind().Bind();

  for (uint32_t i = 1; i < num_constraints; i++) {
    status = buffer_collections[i]->Close();
    if (status != ZX_OK) {
      FX_LOG(ERROR, kTag, "Failed to close producer buffer collection");
      return status;
    }
  }

  return ZX_OK;
}

}  // namespace camera
