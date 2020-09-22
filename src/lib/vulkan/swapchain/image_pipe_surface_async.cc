// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "image_pipe_surface_async.h"

#include <lib/async/cpp/task.h>
#include <lib/fdio/directory.h>
#include <lib/trace/event.h>

#include "vk_dispatch_table_helper.h"
#include "vulkan/vk_layer.h"

namespace image_pipe_swapchain {

bool ImagePipeSurfaceAsync::Init() {
  zx_status_t status = fdio_service_connect("/svc/fuchsia.sysmem.Allocator",
                                            sysmem_allocator_.NewRequest().TakeChannel().release());
  if (status != ZX_OK) {
    fprintf(stderr, "Couldn't connect to sysmem service\n");
    return false;
  }

  return true;
}

bool ImagePipeSurfaceAsync::CreateImage(VkDevice device, VkLayerDispatchTable* pDisp,
                                        VkFormat format, VkImageUsageFlags usage,
                                        VkSwapchainCreateFlagsKHR swapchain_flags,
                                        VkExtent2D extent, uint32_t image_count,
                                        const VkAllocationCallbacks* pAllocator,
                                        std::vector<ImageInfo>* image_info_out) {
  // Allocate token for BufferCollection.
  fuchsia::sysmem::BufferCollectionTokenSyncPtr local_token;
  zx_status_t status = sysmem_allocator_->AllocateSharedCollection(local_token.NewRequest());
  if (status != ZX_OK) {
    fprintf(stderr, "Swapchain: AllocateSharedCollection failed: %d\n", status);
    return false;
  }

  // Duplicate tokens to pass around.
  auto scenic_token = std::make_unique<fuchsia::sysmem::BufferCollectionTokenSyncPtr>();
  status = local_token->Duplicate(std::numeric_limits<uint32_t>::max(), scenic_token->NewRequest());
  if (status != ZX_OK) {
    fprintf(stderr, "Swapchain: Duplicate failed: %d\n", status);
    return false;
  }
  fuchsia::sysmem::BufferCollectionTokenSyncPtr vulkan_token;
  status = local_token->Duplicate(std::numeric_limits<uint32_t>::max(), vulkan_token.NewRequest());
  if (status != ZX_OK) {
    fprintf(stderr, "Swapchain: Duplicate failed: %d\n", status);
    return false;
  }
  status = local_token->Sync();
  if (status != ZX_OK) {
    fprintf(stderr, "Swapchain: Sync failed: %d\n", status);
    return false;
  }

  async::PostTask(loop_.dispatcher(), [this, scenic_token = std::move(scenic_token),
                                       new_buffer_id = ++current_buffer_id_]() {
    // Pass |scenic_token| to Scenic to collect constraints.
    if (image_pipe_.is_bound())
      image_pipe_->AddBufferCollection(new_buffer_id, scenic_token->Unbind());
  });

  // Set swapchain constraints |vulkan_token|.
  VkBufferCollectionCreateInfoFUCHSIA import_info = {
      .sType = VK_STRUCTURE_TYPE_BUFFER_COLLECTION_CREATE_INFO_FUCHSIA,
      .pNext = nullptr,
      .collectionToken = vulkan_token.Unbind().TakeChannel().release(),
  };
  VkBufferCollectionFUCHSIA collection;
  VkResult result =
      pDisp->CreateBufferCollectionFUCHSIA(device, &import_info, pAllocator, &collection);
  if (result != VK_SUCCESS) {
    fprintf(stderr, "Failed to import buffer collection: %d\n", result);
    return false;
  }
  uint32_t image_flags = 0;
  if (swapchain_flags & VK_SWAPCHAIN_CREATE_MUTABLE_FORMAT_BIT_KHR)
    image_flags |= VK_IMAGE_CREATE_MUTABLE_FORMAT_BIT;
  if (swapchain_flags & VK_SWAPCHAIN_CREATE_PROTECTED_BIT_KHR)
    image_flags |= VK_IMAGE_CREATE_PROTECTED_BIT;
  VkImageCreateInfo image_create_info = {
      .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
      .pNext = nullptr,
      .flags = image_flags,
      .imageType = VK_IMAGE_TYPE_2D,
      .format = format,
      .extent = VkExtent3D{extent.width, extent.height, 1},
      .mipLevels = 1,
      .arrayLayers = 1,
      .samples = VK_SAMPLE_COUNT_1_BIT,
      .tiling = VK_IMAGE_TILING_OPTIMAL,
      .usage = usage,
      .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
      .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
  };
  result = pDisp->SetBufferCollectionConstraintsFUCHSIA(device, collection, &image_create_info);
  if (result != VK_SUCCESS) {
    fprintf(stderr, "Failed to set buffer collection constraints: %d\n", result);
    return false;
  }

  // Set |image_count| constraints on the |local_token|.
  fuchsia::sysmem::BufferCollectionSyncPtr buffer_collection;
  status = sysmem_allocator_->BindSharedCollection(std::move(local_token),
                                                   buffer_collection.NewRequest());
  if (status != ZX_OK) {
    fprintf(stderr, "Swapchain: BindSharedCollection failed: %d\n", status);
    return false;
  }
  fuchsia::sysmem::BufferCollectionConstraints constraints;
  constraints.min_buffer_count = image_count;
  constraints.usage.vulkan = fuchsia::sysmem::vulkanUsageSampled;
  status = buffer_collection->SetConstraints(true, constraints);
  if (status != ZX_OK) {
    fprintf(stderr, "Swapchain: SetConstraints failed: %d %d\n", image_count, status);
    return false;
  }

  // Wait for buffer to be allocated.
  zx_status_t allocation_status = ZX_OK;
  fuchsia::sysmem::BufferCollectionInfo_2 buffer_collection_info = {};
  status = buffer_collection->WaitForBuffersAllocated(&allocation_status, &buffer_collection_info);
  if (status != ZX_OK || allocation_status != ZX_OK) {
    fprintf(stderr, "Swapchain: WaitForBuffersAllocated failed: %d\n", status);
    return false;
  }
  if (buffer_collection_info.buffer_count < image_count) {
    fprintf(stderr, "Swapchain: Failed to allocate %d buffers: %d\n", image_count, status);
    return false;
  }

  // Insert width and height information while adding images because it wasnt passed in
  // AddBufferCollection().
  fuchsia::sysmem::ImageFormat_2 image_format = {};
  image_format.coded_width = extent.width;
  image_format.coded_height = extent.height;

  for (uint32_t i = 0; i < image_count; ++i) {
    // Create Vk image.
    VkBufferCollectionImageCreateInfoFUCHSIA image_format_fuchsia = {
        .sType = VK_STRUCTURE_TYPE_BUFFER_COLLECTION_IMAGE_CREATE_INFO_FUCHSIA,
        .pNext = nullptr,
        .collection = collection,
        .index = i};
    image_create_info.pNext = &image_format_fuchsia;
    VkImage image;
    result = pDisp->CreateImage(device, &image_create_info, pAllocator, &image);
    if (result != VK_SUCCESS) {
      fprintf(stderr, "Swapchain: vkCreateImage failed: %d\n", result);
      return false;
    }

    // Extract memory handles from BufferCollection.
    VkMemoryRequirements memory_requirements;
    pDisp->GetImageMemoryRequirements(device, image, &memory_requirements);
    VkBufferCollectionPropertiesFUCHSIA properties = {
        .sType = VK_STRUCTURE_TYPE_BUFFER_COLLECTION_PROPERTIES_FUCHSIA};
    result = pDisp->GetBufferCollectionPropertiesFUCHSIA(device, collection, &properties);
    if (result != VK_SUCCESS) {
      fprintf(stderr, "Swapchain: GetBufferCollectionPropertiesFUCHSIA failed: %d\n", status);
      return false;
    }
    uint32_t memory_type_index =
        __builtin_ctz(memory_requirements.memoryTypeBits & properties.memoryTypeBits);
    VkImportMemoryBufferCollectionFUCHSIA import_info = {
        .sType = VK_STRUCTURE_TYPE_IMPORT_MEMORY_BUFFER_COLLECTION_FUCHSIA,
        .collection = collection,
        .index = i,
    };
    VkMemoryAllocateInfo alloc_info{
        .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .pNext = &import_info,
        .allocationSize = memory_requirements.size,
        .memoryTypeIndex = memory_type_index,
    };
    VkDeviceMemory memory;
    result = pDisp->AllocateMemory(device, &alloc_info, pAllocator, &memory);
    if (result != VK_SUCCESS) {
      fprintf(stderr, "Swapchain: vkAllocateMemory failed: %d\n", result);
      return false;
    }
    result = pDisp->BindImageMemory(device, image, memory, 0);
    if (result != VK_SUCCESS) {
      fprintf(stderr, "Swapchain: vkBindImageMemory failed: %d\n", result);
      return false;
    }

    ImageInfo info = {
        .image = image,
        .memory = memory,
        .image_id = next_image_id(),
    };
    image_info_out->push_back(info);
    async::PostTask(loop_.dispatcher(),
                    [this, info, current_buffer_id = current_buffer_id_, i, image_format]() {
                      std::lock_guard<std::mutex> lock(mutex_);
                      if (image_pipe_.is_bound())
                        image_pipe_->AddImage(info.image_id, current_buffer_id, i, image_format);
                    });

    image_id_to_buffer_id_.emplace(info.image_id, current_buffer_id_);
  }
  buffer_counts_.emplace(current_buffer_id_, image_count);

  pDisp->DestroyBufferCollectionFUCHSIA(device, collection, pAllocator);
  buffer_collection->Close();

  return true;
}

bool ImagePipeSurfaceAsync::IsLost() {
  std::lock_guard<std::mutex> lock(mutex_);
  return channel_closed_;
}

// Disable thread safety analysis because it can't handle unique_lock.
void ImagePipeSurfaceAsync::RemoveImage(uint32_t image_id)
    __attribute__((no_thread_safety_analysis)) {
  std::unique_lock<std::mutex> lock(mutex_);
  for (auto iter = queue_.begin(); iter != queue_.end();) {
    if (iter->image_id == image_id) {
      iter = queue_.erase(iter);
    } else {
      iter++;
    }
  }
  // TODO(fxbug.dev/24315) - remove this workaround
  static constexpr bool kUseWorkaround = true;
  while (kUseWorkaround && present_pending_ && !channel_closed_) {
    lock.unlock();
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
    lock.lock();
  }
  async::PostTask(loop_.dispatcher(), [this, image_id]() {
    if (image_pipe_.is_bound())
      image_pipe_->RemoveImage(image_id);
  });

  // We do not expect same image to be removed multiple times.
  auto buffer_it = buffer_counts_.find(image_id_to_buffer_id_[image_id]);
  if (--buffer_it->second == 0) {
    uint32_t collection_id = buffer_it->first;
    async::PostTask(loop_.dispatcher(), [this, collection_id]() {
      if (image_pipe_.is_bound())
        image_pipe_->RemoveBufferCollection(collection_id);
    });
  }
}

void ImagePipeSurfaceAsync::PresentImage(uint32_t image_id, std::vector<zx::event> acquire_fences,
                                         std::vector<zx::event> release_fences) {
  std::lock_guard<std::mutex> lock(mutex_);
  TRACE_FLOW_BEGIN("gfx", "image_pipe_swapchain_to_present", image_id);

  std::vector<std::unique_ptr<FenceSignaler>> release_fence_signalers;
  release_fence_signalers.reserve(release_fences.size());
  for (auto& fence : release_fences) {
    release_fence_signalers.push_back(std::make_unique<FenceSignaler>(std::move(fence)));
  }
  if (channel_closed_)
    return;
  queue_.push_back({image_id, std::move(acquire_fences), std::move(release_fence_signalers)});
  if (!present_pending_) {
    async::PostTask(loop_.dispatcher(), [this]() {
      std::lock_guard<std::mutex> lock(mutex_);
      PresentNextImageLocked();
    });
  }
}

SupportedImageProperties& ImagePipeSurfaceAsync::GetSupportedImageProperties() {
  return supported_image_properties_;
}

void ImagePipeSurfaceAsync::PresentNextImageLocked() {
  if (present_pending_)
    return;
  if (queue_.empty())
    return;
  TRACE_DURATION("gfx", "ImagePipeSurfaceAsync::PresentNextImageLocked");

  // To guarantee FIFO mode, we can't have Scenic drop any of our frames.
  // We accomplish that sending the next one only when we receive the callback
  // for the previous one.  We don't use the presentation info timing
  // parameters because we really just want to push out the next image asap.
  uint64_t presentation_time = zx_clock_get_monotonic();

  auto& present = queue_.front();
  TRACE_FLOW_END("gfx", "image_pipe_swapchain_to_present", present.image_id);
  TRACE_FLOW_BEGIN("gfx", "image_pipe_present_image", present.image_id);
  if (image_pipe_.is_bound()) {
    std::vector<zx::event> release_events;
    release_events.reserve(present.release_fences.size());
    for (auto& signaler : present.release_fences) {
      zx::event event;
      signaler->event().duplicate(ZX_RIGHT_SAME_RIGHTS, &event);
      release_events.push_back(std::move(event));
    }
    image_pipe_->PresentImage(present.image_id, presentation_time,
                              std::move(present.acquire_fences), std::move(release_events),
                              // Called on the async loop.
                              [this, release_fences = std::move(present.release_fences)](
                                  fuchsia::images::PresentationInfo pinfo) {
                                std::lock_guard<std::mutex> lock(mutex_);
                                present_pending_ = false;
                                for (auto& fence : release_fences) {
                                  fence->reset();
                                }
                                PresentNextImageLocked();
                              });
  }

  queue_.erase(queue_.begin());
  present_pending_ = true;
}

}  // namespace image_pipe_swapchain
