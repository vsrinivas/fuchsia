// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/scenic/lib/gfx/engine/buffer_collection.h"

#include <lib/zx/status.h>
#include <zircon/errors.h>

#include "src/ui/lib/escher/impl/vulkan_utils.h"
#include "src/ui/lib/escher/util/image_utils.h"
#include "src/ui/scenic/lib/gfx/resources/image.h"

namespace scenic_impl::gfx {

fitx::result<fitx::failed, BufferCollectionInfo> BufferCollectionInfo::New(
    fuchsia::sysmem::Allocator_Sync* sysmem_allocator, escher::Escher* escher,
    BufferCollectionHandle buffer_collection_token) {
  if (!buffer_collection_token.is_valid()) {
    FX_LOGS(ERROR) << "Buffer collection token is not valid.";
    return fitx::failed();
  }

  auto vk_device = escher->vk_device();
  FX_DCHECK(vk_device);
  auto vk_loader = escher->device()->dispatch_loader();

  // Create a duped vulkan token.
  fuchsia::sysmem::BufferCollectionTokenSyncPtr vulkan_token;
  {
    // TODO(51213): See if this can become asynchronous.
    fuchsia::sysmem::BufferCollectionTokenSyncPtr sync_token = buffer_collection_token.BindSync();
    zx_status_t status =
        sync_token->Duplicate(std::numeric_limits<uint32_t>::max(), vulkan_token.NewRequest());
    FX_DCHECK(status == ZX_OK);

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
    return fitx::failed();
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

  // From this point on, if we fail, we DCHECK, because we should have already caught errors
  // pertaining to both invalid tokens and wrong/malicious tokens/channels above, meaning that if a
  // failure occurs now, then that is some underlying issue unrelated to user input.
  FX_DCHECK(status == ZX_OK) << "Could not set constraints on buffer collection.";

  vk::ImageCreateInfo create_info =
      escher::image_utils::GetDefaultImageConstraints(vk::Format::eUndefined);

  // Create the vk_collection and set its constraints.
  vk::BufferCollectionFUCHSIA vk_collection;
  {
    vk::BufferCollectionCreateInfoFUCHSIA buffer_collection_create_info;
    buffer_collection_create_info.collectionToken = vulkan_token.Unbind().TakeChannel().release();
    vk_collection = escher::ESCHER_CHECKED_VK_RESULT(
        vk_device.createBufferCollectionFUCHSIA(buffer_collection_create_info, nullptr, vk_loader));
    auto vk_result =
        vk_device.setBufferCollectionConstraintsFUCHSIA(vk_collection, create_info, vk_loader);
    FX_DCHECK(vk_result == vk::Result::eSuccess);
  }

  return fitx::ok(BufferCollectionInfo(std::move(buffer_collection), std::move(vk_collection)));
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

    // Tag the vmos as being a part of scenic.
    for (uint32_t i = 0; i < buffer_collection_info_.buffer_count; ++i) {
      static const char* kVmoName = "ScenicImageMemory";
      buffer_collection_info_.buffers[i].vmo.set_property(ZX_PROP_NAME, kVmoName, strlen(kVmoName));
    }
  }
  return true;
}

fitx::result<fitx::failed, zx::vmo> BufferCollectionInfo::GetVMO(uint32_t index) {
  if (index >= buffer_collection_info_.buffer_count) {
    FX_LOGS(ERROR) << "buffer_collection_index " << index << " is out of bounds.";
    return fitx::failed();
  }

  zx::vmo vmo;
  zx_status_t status =
      buffer_collection_info_.buffers[index].vmo.duplicate(ZX_RIGHT_SAME_RIGHTS, &vmo);

  if (status != ZX_OK) {
    FX_LOGS(ERROR) << "VMO duplication failed.";
    return fitx::failed();
  }

  return fitx::ok(std::move(vmo));
}

}  // namespace scenic_impl::gfx
