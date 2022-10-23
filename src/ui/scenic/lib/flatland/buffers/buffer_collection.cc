// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/scenic/lib/flatland/buffers/buffer_collection.h"

#include <lib/zx/result.h>
#include <zircon/errors.h>

namespace flatland {

fit::result<fit::failed, BufferCollectionInfo> BufferCollectionInfo::New(
    fuchsia::sysmem::Allocator_Sync* sysmem_allocator,
    BufferCollectionHandle buffer_collection_token) {
  FX_DCHECK(sysmem_allocator);

  if (!buffer_collection_token.is_valid()) {
    FX_LOGS(ERROR) << "Buffer collection token is not valid.";
    return fit::failed();
  }

  // Bind the buffer collection token to get the local token. Valid tokens can always be bound,
  // so we do not do any error checking at this stage.
  fuchsia::sysmem::BufferCollectionTokenSyncPtr local_token = buffer_collection_token.BindSync();

  // Use local token to create a BufferCollection and then sync. We can trust
  // |buffer_collection->Sync()| to tell us if we have a bad or malicious channel. So if this call
  // passes, then we know we have a valid BufferCollection.
  fuchsia::sysmem::BufferCollectionSyncPtr buffer_collection;
  sysmem_allocator->BindSharedCollection(std::move(local_token), buffer_collection.NewRequest());
  zx_status_t status = buffer_collection->Sync();
  if (status != ZX_OK) {
    FX_LOGS(ERROR) << "Could not bind buffer collection. Status: " << status;
    return fit::failed();
  }

  // Use a name with a priority thats > the vulkan implementation, but < what any client would use.
  buffer_collection->SetName(10u, "FlatlandImageMemory");

  // Set basic usage constraints, such as requiring at least one buffer and using Vulkan. This is
  // necessary because all clients with a token need to set constraints before the buffer collection
  // can be allocated.
  fuchsia::sysmem::BufferCollectionConstraints constraints;
  constraints.min_buffer_count = 1;
  constraints.usage.vulkan =
      fuchsia::sysmem::vulkanUsageSampled | fuchsia::sysmem::vulkanUsageTransferSrc;
  status = buffer_collection->SetConstraints(true /* has_constraints */, constraints);

  // From this point on, if we fail, we DCHECK, because we should have already caught errors
  // pertaining to both invalid tokens and wrong/malicious tokens/channels above, meaning that if a
  // failure occurs now, then that is some underlying issue unrelated to user input.
  FX_DCHECK(status == ZX_OK) << "Could not set constraints on buffer collection.";

  return fit::ok(BufferCollectionInfo(std::move(buffer_collection)));
}

bool BufferCollectionInfo::BuffersAreAllocated() {
  // If the buffer_collection_info_ struct is already populated, then we know the
  // collection is allocated and we can skip over this code.
  if (!buffer_collection_info_.buffer_count) {
    // Check to see if the buffers are allocated and return false if not.
    zx_status_t allocation_status = ZX_OK;
    zx_status_t status = buffer_collection_ptr_->CheckBuffersAllocated(&allocation_status);
    if (status != ZX_OK || allocation_status != ZX_OK) {
      FX_LOGS(ERROR) << "Collection was not allocated.";
      return false;
    }

    // We still have to call WaitForBuffersAllocated() here in order to fill in
    // the data for buffer_collection_info_. This won't block, since we've already
    // guaranteed that the collection is allocated above.
    status = buffer_collection_ptr_->WaitForBuffersAllocated(&allocation_status,
                                                             &buffer_collection_info_);
    // Failures here would be an issue with sysmem, and so we DCHECK.
    FX_DCHECK(allocation_status == ZX_OK);
    FX_DCHECK(status == ZX_OK);

    // Perform a DCHECK here as well to insure the collection has at least one vmo, because
    // it shouldn't have been able to be allocated with less than that.
    FX_DCHECK(buffer_collection_info_.buffer_count > 0);
  }
  return true;
}

}  // namespace flatland
