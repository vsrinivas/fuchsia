// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "memory_allocation.h"

#include <lib/syslog/global.h>

namespace camera {

zx_status_t ControllerMemoryAllocator::AllocateSharedMemory(
    fuchsia::sysmem::BufferCollectionConstraints producer_constraints,
    fuchsia::sysmem::BufferCollectionConstraints consumer_constraints,
    fuchsia::sysmem::BufferCollectionInfo_2* out_buffer_collection_info) {
  if (out_buffer_collection_info == nullptr) {
    return ZX_ERR_INVALID_ARGS;
  }

  // Create token which we'll hold on to to get our buffer_collection.
  fuchsia::sysmem::BufferCollectionTokenSyncPtr producer_token;
  fuchsia::sysmem::BufferCollectionTokenSyncPtr consumer_token;

  // Start the allocation process.
  auto status = sysmem_allocator_->AllocateSharedCollection(producer_token.NewRequest());
  if (status != ZX_OK) {
    FX_LOGF(ERROR, "", "%s: Failed to create token \n", __func__);
    return status;
  }

  // Duplicate the token.
  status =
      producer_token->Duplicate(std::numeric_limits<uint32_t>::max(), consumer_token.NewRequest());
  if (status != ZX_OK) {
    FX_LOGF(ERROR, "", "%s: Failed to duplicate token \n", __func__);
    return status;
  }
  // Now convert into a Logical BufferCollection:
  fuchsia::sysmem::BufferCollectionSyncPtr producer_buffer_collection;
  fuchsia::sysmem::BufferCollectionSyncPtr consumer_buffer_collection;

  status = sysmem_allocator_->BindSharedCollection(producer_token.Unbind(),
                                                   producer_buffer_collection.NewRequest());
  if (status != ZX_OK) {
    FX_LOGF(ERROR, "", "%s: Failed to create logical buffer collection \n", __func__);
    return status;
  }

  status = producer_buffer_collection->Sync();
  if (status != ZX_OK) {
    FX_LOGF(ERROR, "", "%s: Failed to sync \n", __func__);
    return status;
  }

  status = sysmem_allocator_->BindSharedCollection(consumer_token.Unbind(),
                                                   consumer_buffer_collection.NewRequest());
  if (status != ZX_OK) {
    FX_LOGF(ERROR, "", "%s: Failed to create logical buffer collection \n", __func__);
    return status;
  }

  status = producer_buffer_collection->SetConstraints(true, producer_constraints);
  if (status != ZX_OK) {
    FX_LOGF(ERROR, "", "%s: Failed to set producer buffer collection constraints \n", __func__);
    return status;
  }

  status = consumer_buffer_collection->SetConstraints(true, consumer_constraints);
  if (status != ZX_OK) {
    FX_LOGF(ERROR, "", "%s: Failed to set consumer buffer collection constraints \n", __func__);
    return status;
  }

  zx_status_t allocation_status;
  status = producer_buffer_collection->WaitForBuffersAllocated(&allocation_status,
                                                               out_buffer_collection_info);
  if (status != ZX_OK || allocation_status != ZX_OK) {
    FX_LOGF(ERROR, "", "%s: Failed to  wait for buffer collection info.\n", __func__);
    return status;
  }

  status = producer_buffer_collection->Close();
  if (status != ZX_OK) {
    FX_LOGF(ERROR, "", "%s: Failed to close producer buffer collection \n", __func__);
    return status;
  }

  // TODO(38569): Keep at least one buffer collection around to know about
  // any failures sysmem wants to notify by closing the channel.

  status = consumer_buffer_collection->Close();
  if (status != ZX_OK) {
    FX_LOGF(ERROR, "", "%s: Failed to consumer producer buffer collection \n", __func__);
    return status;
  }

  return ZX_OK;
}
}  // namespace camera
