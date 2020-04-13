// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/scenic/lib/flatland/renderer/buffer_collection.h"

#include "src/lib/fxl/logging.h"

#include <lib/zx/status.h>
#include <zircon/errors.h>

namespace flatland {

fitx::result<fitx::failed, BufferCollectionInfo> BufferCollectionInfo::New(
    fuchsia::sysmem::Allocator_Sync* sysmem_allocator,
    BufferCollectionHandle buffer_collection_token) {
  FXL_DCHECK(sysmem_allocator);

  if (!buffer_collection_token.is_valid()) {
    FXL_LOG(ERROR) << "Buffer collection token is not valid.";
    return fitx::failed();
  }

  // Bind the buffer collection token to get the local token. Valid tokens can always be bound,
  // so we do not do any error checking at this stage.
  fuchsia::sysmem::BufferCollectionTokenSyncPtr local_token = buffer_collection_token.BindSync();

  // Create an extra constraint token that will be kept around as a class member in the event that
  // a client of this class wants to create their own additional constraints.
  // Only log an error here if duplicating the token fails, but allow |BindSharedCollection| and
  // |Sync| below to do the error handling if a failure occurs.
  fuchsia::sysmem::BufferCollectionTokenSyncPtr constraint_token;
  zx_status_t status =
      local_token->Duplicate(std::numeric_limits<uint32_t>::max(), constraint_token.NewRequest());
  if (status != ZX_OK) {
    FXL_LOG(ERROR) << "Could not generate constraint token for buffer.";
  }

  // Use local token to create a BufferCollection and then sync. We can trust
  // |buffer_collection->Sync()| to tell us if we have a bad or malicious channel. So if this call
  // passes, then we know we have a valid BufferCollection.
  fuchsia::sysmem::BufferCollectionSyncPtr buffer_collection;
  sysmem_allocator->BindSharedCollection(std::move(local_token), buffer_collection.NewRequest());
  status = buffer_collection->Sync();
  if (status != ZX_OK) {
    FXL_LOG(ERROR) << "Could not bind buffer collection.";
    return fitx::failed();
  }

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
  FXL_DCHECK(status == ZX_OK) << "Could not set constraints on buffer collection.";

  return fitx::ok(BufferCollectionInfo(std::move(buffer_collection), std::move(constraint_token)));
}

BufferCollectionHandle BufferCollectionInfo::GenerateToken() const {
  FXL_DCHECK(constraint_token_)
      << "The buffer collection is already allocated. It can no longer generate any new tokens.";

  BufferCollectionHandle result;
  zx_status_t status =
      constraint_token_->Duplicate(std::numeric_limits<uint32_t>::max(), result.NewRequest());
  FXL_DCHECK(status == ZX_OK) << "Could not generate a new token for the buffer.";
  return result;
}

bool BufferCollectionInfo::WaitUntilAllocated() {
  // Wait for the buffers to be allocated before adding the first Image.
  if (!buffer_collection_info_.buffer_count) {
    // Close out the constraint token we've been keeping around for clients
    // to set additional constraints with. The buffer collection cannot
    // complete its allocation as long as there exist open tokens that have
    // not had constraints set on them.
    constraint_token_->Close();
    constraint_token_ = nullptr;

    // We should wait for buffers to be allocated and then to be sure, check that
    // they have actually been allocated.
    zx_status_t allocation_status = ZX_OK;
    zx_status_t status = buffer_collection_ptr_->WaitForBuffersAllocated(&allocation_status,
                                                                         &buffer_collection_info_);

    if (status != ZX_OK || allocation_status != ZX_OK) {
      FXL_LOG(ERROR) << "Could not allocate buffers for collection.";
      return false;
    }

    FXL_DCHECK(buffer_collection_info_.buffer_count > 0);
    for (uint32_t i = 0; i < buffer_collection_info_.buffer_count; ++i) {
      static const char* kVmoName = "FlatlandImageMemory";
      buffer_collection_info_.buffers[i].vmo.set_property(ZX_PROP_NAME, kVmoName, strlen(kVmoName));
    }
  }
  return true;
}

}  // namespace flatland
