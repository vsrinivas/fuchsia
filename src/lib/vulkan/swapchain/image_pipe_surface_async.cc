// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "image_pipe_surface_async.h"

#include <fidl/fuchsia.sysmem/cpp/wire.h>
#include <lib/async/cpp/task.h>
#include <lib/fdio/directory.h>
#include <lib/trace/event.h>
#include <lib/ui/scenic/cpp/view_identity.h>
#include <vk_dispatch_table_helper.h>

#include <vulkan/vk_layer.h>

#include "src/lib/fsl/handles/object_info.h"
#include "src/lib/vulkan/swapchain/vulkan_utils.h"

zx::channel allocator_endpoint_for_test;
zx::channel flatland_endpoint_for_test;

extern "C" {
__attribute__((visibility("default"))) bool imagepipe_initialize_service_channel(
    zx::channel allocator_endpoint, zx::channel flatland_endpoint) {
  allocator_endpoint_for_test = std::move(allocator_endpoint);
  flatland_endpoint_for_test = std::move(flatland_endpoint);
  return true;
}
}

namespace image_pipe_swapchain {

namespace {

const char* const kTag = "ImagePipeSurfaceAsync";
const fuchsia::ui::composition::TransformId kRootTransform = {1};

}  // namespace

bool ImagePipeSurfaceAsync::Init() {
  const zx_status_t status = fdio_service_connect(
      "/svc/fuchsia.sysmem.Allocator", sysmem_allocator_.NewRequest().TakeChannel().release());
  if (status != ZX_OK) {
    fprintf(stderr, "%s: Couldn't connect to Sysmem service: %d\n", kTag, status);
    return false;
  }
  sysmem_allocator_->SetDebugClientInfo(fsl::GetCurrentProcessName(), fsl::GetCurrentProcessKoid());

  async::PostTask(loop_.dispatcher(), [this] {
    if (!view_creation_token_.value.is_valid()) {
      fprintf(stderr, "%s: ViewCreationToken is invalid.\n", kTag);
      std::lock_guard<std::mutex> lock(mutex_);
      OnErrorLocked();
      return;
    }

    if (allocator_endpoint_for_test.is_valid()) {
      flatland_allocator_.Bind(std::move(allocator_endpoint_for_test));
    } else {
      const zx_status_t status =
          fdio_service_connect("/svc/fuchsia.ui.composition.Allocator",
                               flatland_allocator_.NewRequest().TakeChannel().release());
      if (status != ZX_OK) {
        fprintf(stderr, "%s: Couldn't connect to Flatland Allocator: %d\n", kTag, status);
        std::lock_guard<std::mutex> lock(mutex_);
        OnErrorLocked();
        return;
      }
    }
    flatland_allocator_.set_error_handler([this](auto status) {
      std::lock_guard<std::mutex> lock(mutex_);
      OnErrorLocked();
    });

    if (flatland_endpoint_for_test.is_valid()) {
      flatland_connection_ =
          simple_present::FlatlandConnection::Create(std::move(flatland_endpoint_for_test), kTag);
    } else {
      flatland_connection_ = simple_present::FlatlandConnection::Create(kTag);
      if (!flatland_connection_) {
        fprintf(stderr, "%s: Couldn't connect to Flatland\n", kTag);
        std::lock_guard<std::mutex> lock(mutex_);
        OnErrorLocked();
        return;
      }
    }
    flatland_connection_->SetErrorCallback([this]() {
      std::lock_guard<std::mutex> lock(mutex_);
      OnErrorLocked();
    });

    fidl::InterfacePtr<fuchsia::ui::composition::ParentViewportWatcher> parent_viewport_watcher;
    flatland_connection_->flatland()->CreateView2(std::move(view_creation_token_),
                                                  scenic::NewViewIdentityOnCreation(), {},
                                                  parent_viewport_watcher.NewRequest());
    flatland_connection_->flatland()->CreateTransform(kRootTransform);
    flatland_connection_->flatland()->SetRootTransform(kRootTransform);
  });

  return true;
}

bool ImagePipeSurfaceAsync::CreateImage(VkDevice device, VkLayerDispatchTable* pDisp,
                                        VkFormat format, VkImageUsageFlags usage,
                                        VkSwapchainCreateFlagsKHR swapchain_flags,
                                        VkExtent2D extent, uint32_t image_count,
                                        const VkAllocationCallbacks* pAllocator,
                                        std::vector<ImageInfo>* image_info_out) {
  // To create BufferCollection, the image must have a valid format.
  if (format == VK_FORMAT_UNDEFINED) {
    fprintf(stderr, "%s: Invalid format: %d\n", kTag, format);
    return false;
  }

  // Allocate token for BufferCollection.
  fuchsia::sysmem::BufferCollectionTokenSyncPtr local_token;
  zx_status_t status = sysmem_allocator_->AllocateSharedCollection(local_token.NewRequest());
  if (status != ZX_OK) {
    fprintf(stderr, "%s: AllocateSharedCollection failed: %d\n", kTag, status);
    return false;
  }

  // Duplicate tokens to pass around.
  auto scenic_token = std::make_unique<fuchsia::sysmem::BufferCollectionTokenSyncPtr>();
  status = local_token->Duplicate(std::numeric_limits<uint32_t>::max(), scenic_token->NewRequest());
  if (status != ZX_OK) {
    fprintf(stderr, "%s: Duplicate failed: %d\n", kTag, status);
    return false;
  }
  fuchsia::sysmem::BufferCollectionTokenSyncPtr vulkan_token;
  status = local_token->Duplicate(std::numeric_limits<uint32_t>::max(), vulkan_token.NewRequest());
  if (status != ZX_OK) {
    fprintf(stderr, "%s: Duplicate failed: %d\n", kTag, status);
    return false;
  }
  status = local_token->Sync();
  if (status != ZX_OK) {
    fprintf(stderr, "%s: Sync failed: %d\n", kTag, status);
    return false;
  }

  fuchsia::ui::composition::BufferCollectionExportToken export_token;
  fuchsia::ui::composition::BufferCollectionImportToken import_token;
  status = zx::eventpair::create(0, &export_token.value, &import_token.value);
  if (status != ZX_OK) {
    fprintf(stderr, "%s: Eventpair create failed: %d\n", kTag, status);
    return false;
  }

  async::PostTask(loop_.dispatcher(), [this, scenic_token = std::move(scenic_token),
                                       export_token = std::move(export_token)]() mutable {
    // Pass |scenic_token| to Scenic to collect constraints.
    if (flatland_allocator_.is_bound()) {
      fuchsia::ui::composition::RegisterBufferCollectionArgs args = {};
      args.set_export_token(std::move(export_token));
      args.set_buffer_collection_token(scenic_token->Unbind());
      args.set_usage(fuchsia::ui::composition::RegisterBufferCollectionUsage::DEFAULT);
      flatland_allocator_->RegisterBufferCollection(std::move(args), [this](auto result) {
        if (result.is_err()) {
          fprintf(stderr, "%s: Flatland Allocator registration failed.\n", kTag);
          std::lock_guard<std::mutex> lock(mutex_);
          OnErrorLocked();
        }
      });
    }
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
  const VkSysmemColorSpaceFUCHSIA kSrgbColorSpace = {
      .sType = VK_STRUCTURE_TYPE_SYSMEM_COLOR_SPACE_FUCHSIA,
      .pNext = nullptr,
      .colorSpace = static_cast<uint32_t>(fuchsia::sysmem::ColorSpaceType::SRGB)};
  const VkSysmemColorSpaceFUCHSIA kYuvColorSpace = {
      .sType = VK_STRUCTURE_TYPE_SYSMEM_COLOR_SPACE_FUCHSIA,
      .pNext = nullptr,
      .colorSpace = static_cast<uint32_t>(fuchsia::sysmem::ColorSpaceType::REC709)};

  VkImageFormatConstraintsInfoFUCHSIA format_info = {
      .sType = VK_STRUCTURE_TYPE_IMAGE_FORMAT_CONSTRAINTS_INFO_FUCHSIA,
      .pNext = nullptr,
      .imageCreateInfo = image_create_info,
      .requiredFormatFeatures = GetFormatFeatureFlagsFromUsage(usage),
      .sysmemPixelFormat = 0u,
      .colorSpaceCount = 1,
      .pColorSpaces = IsYuvFormat(format) ? &kYuvColorSpace : &kSrgbColorSpace,
  };
  VkImageConstraintsInfoFUCHSIA image_constraints_info = {
      .sType = VK_STRUCTURE_TYPE_IMAGE_CONSTRAINTS_INFO_FUCHSIA,
      .pNext = nullptr,
      .formatConstraintsCount = 1,
      .pFormatConstraints = &format_info,
      .bufferCollectionConstraints =
          VkBufferCollectionConstraintsInfoFUCHSIA{
              .sType = VK_STRUCTURE_TYPE_BUFFER_COLLECTION_CONSTRAINTS_INFO_FUCHSIA,
              .pNext = nullptr,
              .minBufferCount = 1,
              .maxBufferCount = 0,
              .minBufferCountForCamping = 0,
              .minBufferCountForDedicatedSlack = 0,
              .minBufferCountForSharedSlack = 0,
          },
      .flags = 0u,
  };

  result = pDisp->SetBufferCollectionImageConstraintsFUCHSIA(device, collection,
                                                             &image_constraints_info);
  if (result != VK_SUCCESS) {
    fprintf(stderr, "Failed to set buffer collection constraints: %d\n", result);
    return false;
  }

  // Set |image_count| constraints on the |local_token|.
  fuchsia::sysmem::BufferCollectionSyncPtr buffer_collection;
  status = sysmem_allocator_->BindSharedCollection(std::move(local_token),
                                                   buffer_collection.NewRequest());
  if (status != ZX_OK) {
    fprintf(stderr, "%s: BindSharedCollection failed: %d\n", kTag, status);
    return false;
  }
  fuchsia::sysmem::BufferCollectionConstraints constraints;
  constraints.min_buffer_count = image_count;
  constraints.usage.vulkan = fuchsia::sysmem::vulkanUsageSampled;
  status = buffer_collection->SetConstraints(true, constraints);
  if (status != ZX_OK) {
    fprintf(stderr, "%s: SetConstraints failed: %d %d\n", kTag, image_count, status);
    return false;
  }

  // Wait for buffer to be allocated.
  zx_status_t allocation_status = ZX_OK;
  fuchsia::sysmem::BufferCollectionInfo_2 buffer_collection_info = {};
  status = buffer_collection->WaitForBuffersAllocated(&allocation_status, &buffer_collection_info);
  if (status != ZX_OK || allocation_status != ZX_OK) {
    fprintf(stderr, "%s: WaitForBuffersAllocated failed: %d\n", kTag, status);
    return false;
  }
  if (buffer_collection_info.buffer_count < image_count) {
    fprintf(stderr, "%s: Failed to allocate %d buffers: %d\n", kTag, image_count, status);
    return false;
  }

  // Insert width and height information while adding images because it wasn't passed in
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
      fprintf(stderr, "%s: vkCreateImage failed: %d\n", kTag, result);
      return false;
    }

    // Extract memory handles from BufferCollection.
    VkMemoryRequirements memory_requirements;
    pDisp->GetImageMemoryRequirements(device, image, &memory_requirements);
    VkBufferCollectionPropertiesFUCHSIA properties = {
        .sType = VK_STRUCTURE_TYPE_BUFFER_COLLECTION_PROPERTIES_FUCHSIA};
    result = pDisp->GetBufferCollectionPropertiesFUCHSIA(device, collection, &properties);
    if (result != VK_SUCCESS) {
      fprintf(stderr, "%s: GetBufferCollectionPropertiesFUCHSIA failed: %d\n", kTag, status);
      return false;
    }
    uint32_t memory_type_index =
        __builtin_ctz(memory_requirements.memoryTypeBits & properties.memoryTypeBits);
    VkMemoryDedicatedAllocateInfoKHR dedicated_info = {
        .sType = VK_STRUCTURE_TYPE_MEMORY_DEDICATED_ALLOCATE_INFO_KHR,
        .image = image,
    };
    VkImportMemoryBufferCollectionFUCHSIA import_info = {
        .sType = VK_STRUCTURE_TYPE_IMPORT_MEMORY_BUFFER_COLLECTION_FUCHSIA,
        .pNext = &dedicated_info,
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
      fprintf(stderr, "%s: vkAllocateMemory failed: %d\n", kTag, result);
      return false;
    }
    result = pDisp->BindImageMemory(device, image, memory, 0);
    if (result != VK_SUCCESS) {
      fprintf(stderr, "%s: vkBindImageMemory failed: %d\n", kTag, result);
      return false;
    }

    ImageInfo info = {
        .image = image,
        .memory = memory,
        .image_id = next_image_id(),
    };
    image_info_out->push_back(info);

    fuchsia::ui::composition::BufferCollectionImportToken import_token_dup;
    zx_status_t status =
        import_token.value.duplicate(ZX_RIGHT_SAME_RIGHTS, &import_token_dup.value);
    if (status != ZX_OK) {
      fprintf(stderr, "%s: Duplicate failed: %d\n", kTag, status);
      return false;
    }
    async::PostTask(loop_.dispatcher(), [this, info, import_token_dup = std::move(import_token_dup),
                                         i, extent]() mutable {
      std::lock_guard<std::mutex> lock(mutex_);
      if (!channel_closed_) {
        fuchsia::ui::composition::ImageProperties image_properties;
        image_properties.set_size({extent.width, extent.height});
        flatland_connection_->flatland()->CreateImage({info.image_id}, std::move(import_token_dup),
                                                      i, std::move(image_properties));
        flatland_connection_->flatland()->SetImageDestinationSize({info.image_id},
                                                                  {extent.width, extent.height});
      }
    });
  }

  pDisp->DestroyBufferCollectionFUCHSIA(device, collection, pAllocator);
  buffer_collection->Close();

  return true;
}

bool ImagePipeSurfaceAsync::IsLost() {
  std::lock_guard<std::mutex> lock(mutex_);
  return channel_closed_;
}

void ImagePipeSurfaceAsync::RemoveImage(uint32_t image_id) {
  std::lock_guard<std::mutex> lock(mutex_);
  for (auto iter = queue_.begin(); iter != queue_.end();) {
    if (iter->image_id == image_id) {
      iter = queue_.erase(iter);
    } else {
      iter++;
    }
  }

  async::PostTask(loop_.dispatcher(), [this, image_id]() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!channel_closed_)
      flatland_connection_->flatland()->ReleaseImage({image_id});
  });
}

void ImagePipeSurfaceAsync::PresentImage(uint32_t image_id,
                                         std::vector<std::unique_ptr<PlatformEvent>> acquire_fences,
                                         std::vector<std::unique_ptr<PlatformEvent>> release_fences,
                                         VkQueue queue) {
  std::lock_guard<std::mutex> lock(mutex_);
  TRACE_FLOW_BEGIN("gfx", "image_pipe_swapchain_to_present", image_id);

  std::vector<std::unique_ptr<FenceSignaler>> release_fence_signalers;
  release_fence_signalers.reserve(release_fences.size());

  for (auto& fence : release_fences) {
    zx::event event = static_cast<FuchsiaEvent*>(fence.get())->Take();
    release_fence_signalers.push_back(std::make_unique<FenceSignaler>(std::move(event)));
  }

  if (channel_closed_)
    return;

  std::vector<zx::event> acquire_events;
  acquire_events.reserve(acquire_fences.size());
  for (auto& fence : acquire_fences) {
    zx::event event = static_cast<FuchsiaEvent*>(fence.get())->Take();
    acquire_events.push_back(std::move(event));
  }

  queue_.push_back({image_id, std::move(acquire_events), std::move(release_fence_signalers)});

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
  // We accomplish that by setting unsquashable flag.
  uint64_t presentation_time = zx_clock_get_monotonic();

  auto& present = queue_.front();
  TRACE_FLOW_END("gfx", "image_pipe_swapchain_to_present", present.image_id);
  TRACE_FLOW_BEGIN("gfx", "Flatland::Present", present.image_id);
  if (!channel_closed_) {
    std::vector<zx::event> release_events;
    release_events.reserve(present.release_fences.size());
    for (auto& signaler : present.release_fences) {
      zx::event event;
      signaler->event().duplicate(ZX_RIGHT_SAME_RIGHTS, &event);
      release_events.push_back(std::move(event));
    }

    // In Flatland, release fences apply to the content of the previous present. Keeping track of
    // the previous frame's release fences and swapping ensure we set the correct ones. When the
    // current frame's OnFramePresented callback is called, it is safe to stop tracking the
    // previous frame's |release_fences|.
    previous_present_release_fences_.swap(present.release_fences);

    fuchsia::ui::composition::PresentArgs present_args;
    present_args.set_requested_presentation_time(presentation_time);
    present_args.set_acquire_fences(std::move(present.acquire_fences));
    present_args.set_release_fences(std::move(release_events));
    present_args.set_unsquashable(true);
    flatland_connection_->flatland()->SetContent(kRootTransform, {present.image_id});
    flatland_connection_->Present(std::move(present_args),
                                  // Called on the async loop.
                                  [this, release_fences = std::move(present.release_fences)](
                                      zx_time_t actual_presentation_time) {
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

void ImagePipeSurfaceAsync::OnErrorLocked() {
  channel_closed_ = true;
  queue_.clear();
  flatland_connection_.reset();
  previous_present_release_fences_.clear();
}

}  // namespace image_pipe_swapchain
