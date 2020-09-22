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
    fuchsia::sysmem::BufferCollectionInfo_2* out_buffer_collection_info) const {
  TRACE_DURATION("camera", "ControllerMemoryAllocator::AllocateSharedMemory");
  if (out_buffer_collection_info == nullptr) {
    return ZX_ERR_INVALID_ARGS;
  }

  auto num_constraints = constraints.size();

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
    status = buffer_collections[i]->SetConstraints(true, constraints[i]);
    if (status != ZX_OK) {
      FX_LOG(ERROR, kTag, "Failed to set buffer collection constraints");
      return status;
    }
  }

  zx_status_t allocation_status;
  status = buffer_collections[0]->WaitForBuffersAllocated(&allocation_status,
                                                          out_buffer_collection_info);
  if (status != ZX_OK || allocation_status != ZX_OK) {
    FX_LOG(ERROR, kTag, "Failed to wait for buffer collection info.");
    return status;
  }

  for (uint32_t i = 0; i < num_constraints; i++) {
    status = buffer_collections[i]->Close();
    if (status != ZX_OK) {
      FX_LOG(ERROR, kTag, "Failed to close producer buffer collection");
      return status;
    }
  }

  // TODO(fxbug.dev/38569): Keep at least one buffer collection around to know about
  // any failures sysmem wants to notify by closing the channel.
  return ZX_OK;
}

}  // namespace camera
