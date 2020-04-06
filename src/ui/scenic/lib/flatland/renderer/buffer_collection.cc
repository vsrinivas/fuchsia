// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/scenic/lib/flatland/renderer/buffer_collection.h"

#include "src/lib/fxl/logging.h"

namespace {

// We need to make sure that the buffer is Vulkan compatible, so this function sets
// Vulkan-specific constraints on the buffer.
bool SetVKBufferCollectionConstraints(const vk::Device& vk_device,
                                      const vk::DispatchLoaderDynamic& vk_loader,
                                      const vk::ImageCreateInfo& create_info,
                                      fuchsia::sysmem::BufferCollectionTokenSyncPtr token,
                                      vk::BufferCollectionFUCHSIA* out_buffer_collection_fuchsia) {
  // Set VkImage constraints using |create_info| on |token|
  FXL_DCHECK(vk_device);
  FXL_DCHECK(vk_loader.vkCreateBufferCollectionFUCHSIA);

  vk::BufferCollectionCreateInfoFUCHSIA buffer_collection_create_info;
  buffer_collection_create_info.collectionToken = token.Unbind().TakeChannel().release();
  auto create_buffer_collection_result =
      vk_device.createBufferCollectionFUCHSIA(buffer_collection_create_info, nullptr, vk_loader);
  if (create_buffer_collection_result.result != vk::Result::eSuccess) {
    return false;
  }

  auto constraints_result = vk_device.setBufferCollectionConstraintsFUCHSIA(
      create_buffer_collection_result.value, create_info, vk_loader);
  if (constraints_result != vk::Result::eSuccess) {
    return false;
  }

  *out_buffer_collection_fuchsia = create_buffer_collection_result.value;
  return true;
}

}  // anonymous namespace

namespace flatland {

std::unique_ptr<BufferCollectionInfo> BufferCollectionInfo::CreateWithConstraints(
    const vk::Device& device, const vk::DispatchLoaderDynamic& vk_loader,
    fuchsia::sysmem::Allocator_Sync* sysmem_allocator,
    const vk::ImageCreateInfo& vulkan_image_constraints,
    BufferCollectionHandle buffer_collection_token) {
  FXL_DCHECK(sysmem_allocator);
  FXL_DCHECK((vulkan_image_constraints.usage &
              (vk::ImageUsageFlagBits::eTransferSrc | vk::ImageUsageFlagBits::eSampled)) ==
             (vk::ImageUsageFlagBits::eTransferSrc | vk::ImageUsageFlagBits::eSampled));

  if (!buffer_collection_token.is_valid()) {
    FXL_LOG(ERROR) << "Buffer collection token is not valid.";
    return nullptr;
  }

  // Bind the buffer collection token to get the local token. Valid tokens can always be bound,
  // so we do not do any error checking at this stage.
  fuchsia::sysmem::BufferCollectionTokenSyncPtr local_token = buffer_collection_token.BindSync();

  // Only log an error here if duplicating the token fails, but allow |BindSharedCollection| and
  // |Sync| below to do the error handling if a failure occurs.
  fuchsia::sysmem::BufferCollectionTokenSyncPtr vulkan_token;
  zx_status_t status =
      local_token->Duplicate(std::numeric_limits<uint32_t>::max(), vulkan_token.NewRequest());
  if (status != ZX_OK) {
    FXL_LOG(ERROR) << "Could not generate vulkan token for buffer.";
  }

  // Use local token to create a BufferCollection and then sync. We can trust
  // |buffer_collection->Sync()| to tell us if we have a bad or malicious channel. So if this call
  // passes, then we know we have a valid BufferCollection.
  fuchsia::sysmem::BufferCollectionSyncPtr buffer_collection;
  sysmem_allocator->BindSharedCollection(std::move(local_token), buffer_collection.NewRequest());
  status = buffer_collection->Sync();
  if (status != ZX_OK) {
    FXL_LOG(ERROR) << "Could not bind buffer collection.";
    return nullptr;
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

  // Create a vulkan buffer collection object, and set VkImage constraints, separately from
  // the sysmem constraints set above.
  vk::BufferCollectionFUCHSIA vk_buffer_collection;
  const bool vk_constraints_set = SetVKBufferCollectionConstraints(
      device, vk_loader, vulkan_image_constraints, std::move(vulkan_token), &vk_buffer_collection);
  FXL_DCHECK(vk_constraints_set) << "Could not set vk constraints on buffer collection.";

  return std::unique_ptr<BufferCollectionInfo>(
      new BufferCollectionInfo(std::move(buffer_collection), vk_buffer_collection));
}

bool BufferCollectionInfo::WaitUntilAllocated() {
  // Wait for the buffers to be allocated before adding the first Image.
  if (!buffer_collection_info_.buffer_count) {
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

void BufferCollectionInfo::Destroy(const vk::Device& device,
                                   const vk::DispatchLoaderDynamic& vk_loader) {
  FXL_DCHECK(device);

  // Close the connection.
  buffer_collection_ptr_->Close();
  buffer_collection_ptr_ = nullptr;

  // Destroy the vkBufferCollection object.
  device.destroyBufferCollectionFUCHSIA(vk_buffer_collection_, nullptr, vk_loader);
  vk_buffer_collection_ = nullptr;
}

}  // namespace flatland
