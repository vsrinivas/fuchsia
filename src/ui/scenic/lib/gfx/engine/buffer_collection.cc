// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/scenic/lib/gfx/engine/buffer_collection.h"

#include <lib/zx/status.h>
#include <zircon/errors.h>

#include "src/ui/lib/escher/impl/vulkan_utils.h"
#include "src/ui/lib/escher/util/fuchsia_utils.h"
#include "src/ui/lib/escher/util/image_utils.h"
#include "src/ui/scenic/lib/gfx/resources/image.h"

namespace scenic_impl::gfx {

fit::result<fit::failed, BufferCollectionInfo> BufferCollectionInfo::New(
    fuchsia::sysmem::Allocator_Sync* sysmem_allocator, escher::Escher* escher,
    BufferCollectionHandle buffer_collection_token) {
  if (!buffer_collection_token.is_valid()) {
    FX_LOGS(ERROR) << "Buffer collection token is not valid.";
    return fit::failed();
  }

  auto vk_device = escher->vk_device();
  FX_DCHECK(vk_device);
  auto vk_loader = escher->device()->dispatch_loader();

  // Create a duped vulkan token.
  fuchsia::sysmem::BufferCollectionTokenSyncPtr vulkan_token;
  {
    // TODO(fxbug.dev/51213): See if this can become asynchronous.
    fuchsia::sysmem::BufferCollectionTokenSyncPtr sync_token = buffer_collection_token.BindSync();
    zx_status_t status =
        sync_token->Duplicate(std::numeric_limits<uint32_t>::max(), vulkan_token.NewRequest());
    if (status != ZX_OK) {
      FX_LOGS(ERROR) << "Cannot duplicate token. The client may have invalidated the token.";
      return fit::failed();
    }

    // Reassign the channel to the non-sync interface handle.
    buffer_collection_token = sync_token.Unbind();
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
    FX_LOGS(ERROR) << "Could not bind buffer collection.";
    return fit::failed();
  }

  // Set a friendly name if currently unset.
  const char* kVmoName = "GFXBufferCollection";
  // Set the name priority to 20 to override what Vulkan might set, but allow
  // the application to have a higher priority.
  constexpr uint32_t kNamePriority = 20;
  buffer_collection->SetName(kNamePriority, kVmoName);

  // Set basic usage constraints, such as requiring at least one buffer and using Vulkan. This is
  // necessary because all clients with a token need to set constraints before the buffer collection
  // can be allocated.
  fuchsia::sysmem::BufferCollectionConstraints constraints;
  constraints.min_buffer_count = 1;
  constraints.usage.vulkan =
      fuchsia::sysmem::vulkanUsageSampled | fuchsia::sysmem::vulkanUsageTransferSrc;
  status = buffer_collection->SetConstraints(true /* has_constraints */, constraints);

  // If a client requests to create Image2 / Image3 but then terminates before
  // Scenic completes the import. The sysmem will close all the other handles
  // to the BufferCollection, and all the buffer collection operations will
  // fail, including the Vulkan buffer collection calls. Thus we should still
  // return a fit::failed here (and in checks below) instead of crashing
  // Scenic.
  if (status != ZX_OK) {
    FX_LOGS(ERROR) << "Could not set constraints on buffer collection: " << status;
    return fit::failed();
  }

  vk::ImageCreateInfo create_info =
      escher::image_utils::GetDefaultImageConstraints(vk::Format::eUndefined);

  auto image_constraints_info =
      escher::GetDefaultImageConstraintsInfo(create_info, escher->allow_protected_memory());

  // Create the vk_collection and set its constraints.
  vk::BufferCollectionFUCHSIA vk_collection;
  {
    vk::BufferCollectionCreateInfoFUCHSIA buffer_collection_create_info;
    buffer_collection_create_info.collectionToken = vulkan_token.Unbind().TakeChannel().release();
    vk_collection = escher::ESCHER_CHECKED_VK_RESULT(
        vk_device.createBufferCollectionFUCHSIA(buffer_collection_create_info, nullptr, vk_loader));
    auto vk_result = vk_device.setBufferCollectionImageConstraintsFUCHSIA(
        vk_collection, image_constraints_info.image_constraints, vk_loader);
    if (vk_result != vk::Result::eSuccess) {
      FX_LOGS(ERROR) << "Could not call vkSetBufferCollectionImageConstraintsFUCHSIA: "
                     << vk::to_string(vk_result);
      return fit::failed();
    }
  }

  return fit::ok(BufferCollectionInfo(std::move(buffer_collection), std::move(vk_collection)));
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
    if (status != ZX_OK || allocation_status != ZX_OK) {
      FX_LOGS(ERROR) << "WaitForBuffersAllocated failed: " << status << ":" << allocation_status;
      return false;
    }

    // Perform a DCHECK here as well to insure the collection has at least one vmo, because
    // it shouldn't have been able to be allocated with less than that.
    FX_DCHECK(buffer_collection_info_.buffer_count > 0);
  }
  return true;
}

fit::result<fit::failed, zx::vmo> BufferCollectionInfo::GetVMO(uint32_t index) {
  if (index >= buffer_collection_info_.buffer_count) {
    FX_LOGS(ERROR) << "buffer_collection_index " << index << " is out of bounds.";
    return fit::failed();
  }

  zx::vmo vmo;
  zx_status_t status =
      buffer_collection_info_.buffers[index].vmo.duplicate(ZX_RIGHT_SAME_RIGHTS, &vmo);

  if (status != ZX_OK) {
    FX_LOGS(ERROR) << "VMO duplication failed.";
    return fit::failed();
  }

  return fit::ok(std::move(vmo));
}

}  // namespace scenic_impl::gfx
