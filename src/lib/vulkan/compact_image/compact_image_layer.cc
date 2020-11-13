// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/sysmem/cpp/fidl.h>
#include <lib/fdio/directory.h>
#include <vk_dispatch_table_helper.h>
#include <vk_layer_data.h>
#include <vk_layer_extension_utils.h>
#include <vk_layer_utils_minimal.h>

#include <unordered_map>
#include <vector>

#include <fbl/auto_call.h>
#include <sdk/lib/syslog/cpp/macros.h>
#include <vulkan/vk_layer.h>

#define VK_LAYER_API_VERSION VK_MAKE_VERSION(1, 1, VK_HEADER_VERSION)

namespace compact_image {

constexpr VkExtensionProperties device_extensions[] = {{
    .extensionName = "VK_FUCHSIA_compact_image",
    .specVersion = 1,
}};

constexpr VkLayerProperties compact_image_layer = {
    "VK_LAYER_FUCHSIA_compact_image",
    VK_LAYER_API_VERSION,
    1,
    "Compact Image",
};

//
// Shaders
//

#include "pack.spv.h"
#include "unpack.spv.h"

// Push constant block used by all shaders.
struct PushConstantBlock {
  VkDeviceAddress image_address;
  VkDeviceAddress scratch_address;
  VkDeviceAddress aux_address;
  uint32_t body_offset;
  uint32_t block_count;
};

//
// AFBC constants
//

constexpr uint32_t kAfbcBodyAlignment = 4096u;
constexpr uint32_t kAfbcWidthAlignment = 128u;
constexpr uint32_t kAfbcHeightAlignment = 128u;
constexpr uint32_t kAfbcTilePixelWidth = 16u;
constexpr uint32_t kAfbcTilePixelHeight = 16u;
constexpr uint32_t kTileBytesPerPixel = 4u;
constexpr uint32_t kTileNumPixels = kAfbcTilePixelWidth * kAfbcTilePixelHeight;
constexpr uint32_t kTileNumBytes = kTileNumPixels * kTileBytesPerPixel;
constexpr uint32_t kAfbcBytesPerTileHeader = 16u;
constexpr uint32_t kAfbcSuperblockTileWidth = 8u;
constexpr uint32_t kAfbcSuperblockTileHeight = 8u;
constexpr uint32_t kAfbcSuperblockPixelWidth = kAfbcSuperblockTileWidth * kAfbcTilePixelWidth;
constexpr uint32_t kAfbcSuperblockPixelHeight = kAfbcSuperblockTileHeight * kAfbcTilePixelHeight;
constexpr uint32_t kAfbcSuperblockTileCount = kAfbcSuperblockTileWidth * kAfbcSuperblockTileHeight;
constexpr uint32_t kAfbcBytesPerSuperblockHeader =
    kAfbcSuperblockTileCount * kAfbcBytesPerTileHeader;

//
// Vulkan utility functions
//

struct vk_struct_common {
  VkStructureType sType;
  struct vk_struct_common* pNext;
};

#define vk_foreach_struct(__iter, __start)                                            \
  for (struct vk_struct_common* __iter = (struct vk_struct_common*)(__start); __iter; \
       __iter = __iter->pNext)

#define vk_foreach_struct_const(__iter, __start)                                                  \
  for (const struct vk_struct_common* __iter = (const struct vk_struct_common*)(__start); __iter; \
       __iter = __iter->pNext)

inline void* __vk_find_struct(void* start, VkStructureType sType) {
  vk_foreach_struct(s, start) {
    if (s->sType == sType)
      return s;
  }
  return nullptr;
}

#define vk_find_struct(__start, __sType) __vk_find_struct((__start), VK_STRUCTURE_TYPE_##__sType)

#define vk_find_struct_const(__start, __sType) \
  (const void*)__vk_find_struct((void*)(__start), VK_STRUCTURE_TYPE_##__sType)

//
// AFBC image compactor
//
// This class implements packing and unpacking of AFBC images
// when transitioning to/from image layouts that support packed
// mode.
//
// Compute shaders are used to pack/unpack images and dedicated
// memory allocations are required to control the exact tiling
// format used for images.
//
class ImageCompactor {
 public:
  ImageCompactor(VkDevice device, VkLayerDispatchTable* dispatch)
      : device_(device), dispatch_(dispatch) {}

  // Returns true if vendor ID and device ID are supported.
  static bool IsSupportedGPU(uint32_t vendor_id, uint32_t device_id) {
    switch (vendor_id) {
      case 0x13b5:  // ARM
        switch (device_id) {
          case 0x70930000:  // BIFROST4
          case 0x72120000:  // BIFROST8
            return true;
          default:
            break;
        }
      default:
        break;
    }
    return false;
  }

  // Returns compact image format properties if supported.
  static VkResult GetImageFormatProperties(const VkPhysicalDeviceProperties& gpu_properties,
                                           VkFormat format, VkImageType type, VkImageTiling tiling,
                                           VkImageUsageFlags usage, VkImageCreateFlags flags,
                                           VkImageFormatProperties* pImageFormatProperties) {
    if (!IsSupportedGPU(gpu_properties.vendorID, gpu_properties.deviceID)) {
      return VK_ERROR_FORMAT_NOT_SUPPORTED;
    }

    if (!IsSupportedFormat(format) || !IsSupportedUsage(usage)) {
      return VK_ERROR_FORMAT_NOT_SUPPORTED;
    }

    // 2D image type is supported.
    if (type != VK_IMAGE_TYPE_2D) {
      return VK_ERROR_FORMAT_NOT_SUPPORTED;
    }

    // Compaction is only supported for optimal tiling images.
    if (tiling != VK_IMAGE_TILING_OPTIMAL) {
      return VK_ERROR_FORMAT_NOT_SUPPORTED;
    }

    // Mutable formats are not supported.
    if (flags & VK_IMAGE_CREATE_MUTABLE_FORMAT_BIT) {
      return VK_ERROR_FORMAT_NOT_SUPPORTED;
    }

    // Return compact image properties.
    *pImageFormatProperties = VkImageFormatProperties{
        .maxExtent = VkExtent3D{.width = 8192, .height = 8192, .depth = 1},
        .maxMipLevels = 1,
        .maxArrayLayers = 1,
        .sampleCounts = 1,
        .maxResourceSize = 0x80000000,
    };
    return VK_SUCCESS;
  }

  // Initialize image compactor. Connects to sysmem and creates
  // compute pipelines needed for packing and unpacking.
  VkResult Init(PFN_vkGetDeviceProcAddr fpGetDeviceProcAddr,
                const VkAllocationCallbacks* pAllocator) {
    // Connect to sysmem allocator service.
    zx_status_t status = fdio_service_connect(
        "/svc/fuchsia.sysmem.Allocator", sysmem_allocator_.NewRequest().TakeChannel().release());
    if (status != ZX_OK) {
      fprintf(stderr, "Couldn't connect to sysmem service\n");
      return VK_ERROR_INITIALIZATION_FAILED;
    }

    auto cleanup = fbl::MakeAutoCall([this, pAllocator] { Cleanup(pAllocator); });

    // Create pipeline layout that will be used by all shaders.
    VkResult result =
        CreatePipelineLayout(sizeof(PushConstantBlock), pAllocator, &pipeline_layout_);
    if (result != VK_SUCCESS) {
      return VK_ERROR_INITIALIZATION_FAILED;
    }

    // Create compute pipeline used to pack AFBC images.
    result = CreateComputePipeline(pack_comp_spv, sizeof(pack_comp_spv), pipeline_layout_,
                                   pAllocator, &pack_pipeline_);
    if (result != VK_SUCCESS) {
      return VK_ERROR_INITIALIZATION_FAILED;
    }

    // Create compute pipeline used to unpack AFBC images.
    result = CreateComputePipeline(unpack_comp_spv, sizeof(unpack_comp_spv), pipeline_layout_,
                                   pAllocator, &unpack_pipeline_);
    if (result != VK_SUCCESS) {
      return VK_ERROR_INITIALIZATION_FAILED;
    }

    result = CreateBuffer(kTileNumBytes * kAfbcSuperblockTileCount, nullptr, pAllocator,
                          &scratch_.buffer);
    if (result != VK_SUCCESS) {
      return VK_ERROR_INITIALIZATION_FAILED;
    }
    result = AllocateAndBindBufferMemory(scratch_.buffer, nullptr, pAllocator, &scratch_.memory,
                                         &scratch_.device_address);
    if (result != VK_SUCCESS) {
      return VK_ERROR_INITIALIZATION_FAILED;
    }

    cleanup.cancel();
    return VK_SUCCESS;
  }

  void Cleanup(const VkAllocationCallbacks* pAllocator) {
    if (scratch_.buffer != VK_NULL_HANDLE) {
      dispatch_->DestroyBuffer(device_, scratch_.buffer, pAllocator);
    }
    if (scratch_.memory != VK_NULL_HANDLE) {
      dispatch_->FreeMemory(device_, scratch_.memory, pAllocator);
    }
    if (pack_pipeline_ != VK_NULL_HANDLE) {
      dispatch_->DestroyPipeline(device_, pack_pipeline_, pAllocator);
    }
    if (unpack_pipeline_ != VK_NULL_HANDLE) {
      dispatch_->DestroyPipeline(device_, unpack_pipeline_, pAllocator);
    }
    if (pipeline_layout_ != VK_NULL_HANDLE) {
      dispatch_->DestroyPipelineLayout(device_, pipeline_layout_, pAllocator);
    }
  }

  VkResult CreateImage(const VkImageCreateInfo* pCreateInfo,
                       const VkAllocationCallbacks* pAllocator, VkImage* pImage) {
    // Early out if this is a regular image.
    if (!(pCreateInfo->flags & VK_IMAGE_CREATE_COMPACT_BIT_FUCHSIA)) {
      return dispatch_->CreateImage(device_, pCreateInfo, pAllocator, pImage);
    }

    FX_CHECK(IsSupportedFormat(pCreateInfo->format));
    FX_CHECK(IsSupportedUsage(pCreateInfo->usage));
    FX_CHECK(pCreateInfo->imageType == VK_IMAGE_TYPE_2D);
    FX_CHECK(pCreateInfo->tiling == VK_IMAGE_TILING_OPTIMAL);
    FX_CHECK(!(pCreateInfo->flags & VK_IMAGE_CREATE_MUTABLE_FORMAT_BIT));
    FX_CHECK(pCreateInfo->mipLevels == 1);
    FX_CHECK(pCreateInfo->arrayLayers == 1);
    FX_CHECK(pCreateInfo->samples == VK_SAMPLE_COUNT_1_BIT);

    // Calculate superblock dimensions for image.
    uint32_t width_in_superblocks =
        RoundUp(pCreateInfo->extent.width, kAfbcWidthAlignment) / kAfbcSuperblockPixelWidth;
    uint32_t height_in_superblocks =
        RoundUp(pCreateInfo->extent.height, kAfbcHeightAlignment) / kAfbcSuperblockPixelHeight;
    uint32_t num_superblocks = width_in_superblocks * height_in_superblocks;
    uint32_t body_offset =
        RoundUp(num_superblocks * kAfbcBytesPerSuperblockHeader, kAfbcBodyAlignment);

    // Create single buffer collection for image.
    fuchsia::sysmem::BufferCollectionTokenSyncPtr local_token;
    zx_status_t status = sysmem_allocator_->AllocateSharedCollection(local_token.NewRequest());
    if (status != ZX_OK) {
      return VK_ERROR_INITIALIZATION_FAILED;
    }
    fuchsia::sysmem::BufferCollectionTokenSyncPtr vulkan_image_token;
    status = local_token->Duplicate(std::numeric_limits<uint32_t>::max(),
                                    vulkan_image_token.NewRequest());
    if (status != ZX_OK) {
      return VK_ERROR_INITIALIZATION_FAILED;
    }
    fuchsia::sysmem::BufferCollectionTokenSyncPtr vulkan_buffer_token;
    status = local_token->Duplicate(std::numeric_limits<uint32_t>::max(),
                                    vulkan_buffer_token.NewRequest());
    if (status != ZX_OK) {
      return VK_ERROR_INITIALIZATION_FAILED;
    }
    status = local_token->Sync();
    if (status != ZX_OK) {
      return VK_ERROR_INITIALIZATION_FAILED;
    }

    VkBufferCollectionCreateInfoFUCHSIA image_collection_create_info = {
        .sType = VK_STRUCTURE_TYPE_BUFFER_COLLECTION_CREATE_INFO_FUCHSIA,
        .pNext = nullptr,
        .collectionToken = vulkan_image_token.Unbind().TakeChannel().release(),
    };
    VkBufferCollectionFUCHSIA collection;
    VkResult result = dispatch_->CreateBufferCollectionFUCHSIA(
        device_, &image_collection_create_info, nullptr, &collection);
    if (result != VK_SUCCESS) {
      return VK_ERROR_INITIALIZATION_FAILED;
    }

    auto cleanup_collection = fbl::MakeAutoCall([this, collection, pAllocator] {
      dispatch_->DestroyBufferCollectionFUCHSIA(device_, collection, pAllocator);
    });

    VkSysmemColorSpaceFUCHSIA color_space = {
        .colorSpace = static_cast<uint32_t>(fuchsia::sysmem::ColorSpaceType::SRGB),
    };
    VkImageFormatConstraintsInfoFUCHSIA image_format_constraints_info = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_CONSTRAINTS_INFO_FUCHSIA,
        .pNext = nullptr,
        .requiredFormatFeatures = 0,
        .flags = 0,
        .sysmemFormat = static_cast<uint32_t>(fuchsia::sysmem::PixelFormatType::R8G8B8A8),
        .colorSpaceCount = 1,
        .pColorSpaces = &color_space,
    };
    VkImageConstraintsInfoFUCHSIA image_constraints_info = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_CONSTRAINTS_INFO_FUCHSIA,
        .pNext = nullptr,
        .createInfoCount = 1,
        .pCreateInfos = pCreateInfo,
        .pFormatConstraints = &image_format_constraints_info,
        .minBufferCount = 1,
        .maxBufferCount = 1,
        .minBufferCountForDedicatedSlack = 0,
        .minBufferCountForSharedSlack = 0,
    };
    result = dispatch_->SetBufferCollectionImageConstraintsFUCHSIA(device_, collection,
                                                                   &image_constraints_info);
    if (result != VK_SUCCESS) {
      return VK_ERROR_INITIALIZATION_FAILED;
    }

    VkBufferCollectionCreateInfoFUCHSIA buffer_collection_create_info = {
        .sType = VK_STRUCTURE_TYPE_BUFFER_COLLECTION_CREATE_INFO_FUCHSIA,
        .pNext = nullptr,
        .collectionToken = vulkan_buffer_token.Unbind().TakeChannel().release(),
    };
    VkBufferCollectionFUCHSIA collection_for_buffer;
    result = dispatch_->CreateBufferCollectionFUCHSIA(device_, &buffer_collection_create_info,
                                                      nullptr, &collection_for_buffer);
    if (result != VK_SUCCESS) {
      return VK_ERROR_INITIALIZATION_FAILED;
    }

    auto cleanup_collection_for_buffer =
        fbl::MakeAutoCall([this, collection_for_buffer, pAllocator] {
          dispatch_->DestroyBufferCollectionFUCHSIA(device_, collection_for_buffer, pAllocator);
        });

    VkBufferCreateInfo buffer_create_info = {
        .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .pNext = nullptr,
        .flags = 0,
        .size = body_offset + num_superblocks * kAfbcSuperblockTileCount * kTileNumBytes,
        .usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
        .queueFamilyIndexCount = 0,
        .pQueueFamilyIndices = nullptr,
    };
    VkBufferConstraintsInfoFUCHSIA buffer_constraints_info = {
        .sType = VK_STRUCTURE_TYPE_BUFFER_CONSTRAINTS_INFO_FUCHSIA,
        .pNext = nullptr,
        .pBufferCreateInfo = &buffer_create_info,
        .requiredFormatFeatures = 0,
        .minCount = 1,
    };
    result = dispatch_->SetBufferCollectionBufferConstraintsFUCHSIA(device_, collection_for_buffer,
                                                                    &buffer_constraints_info);
    if (result != VK_SUCCESS) {
      return VK_ERROR_INITIALIZATION_FAILED;
    }

    fuchsia::sysmem::BufferCollectionSyncPtr buffer_collection;
    status = sysmem_allocator_->BindSharedCollection(std::move(local_token),
                                                     buffer_collection.NewRequest());
    if (status != ZX_OK) {
      return VK_ERROR_INITIALIZATION_FAILED;
    }

    fuchsia::sysmem::BufferCollectionConstraints constraints;
    constraints.min_buffer_count = 1;
    constraints.usage.vulkan = 0;
    if (pCreateInfo->usage & VK_IMAGE_USAGE_TRANSFER_SRC_BIT) {
      constraints.usage.vulkan |= fuchsia::sysmem::VULKAN_IMAGE_USAGE_TRANSFER_SRC;
    }
    if (pCreateInfo->usage & VK_IMAGE_USAGE_TRANSFER_DST_BIT) {
      constraints.usage.vulkan |= fuchsia::sysmem::VULKAN_IMAGE_USAGE_TRANSFER_DST;
    }
    if (pCreateInfo->usage & VK_IMAGE_USAGE_SAMPLED_BIT) {
      constraints.usage.vulkan |= fuchsia::sysmem::VULKAN_IMAGE_USAGE_SAMPLED;
    }
    constraints.has_buffer_memory_constraints = true;
    constraints.buffer_memory_constraints.ram_domain_supported = true;
    constraints.buffer_memory_constraints.cpu_domain_supported = true;
    constraints.buffer_memory_constraints.inaccessible_domain_supported = true;
    constraints.image_format_constraints_count = 1;
    fuchsia::sysmem::ImageFormatConstraints& image_constraints =
        constraints.image_format_constraints[0];
    image_constraints = fuchsia::sysmem::ImageFormatConstraints();
    image_constraints.min_coded_width = pCreateInfo->extent.width;
    image_constraints.min_coded_height = pCreateInfo->extent.height;
    image_constraints.max_coded_width = pCreateInfo->extent.width;
    image_constraints.max_coded_height = pCreateInfo->extent.height;
    image_constraints.min_bytes_per_row = 0;
    image_constraints.pixel_format.type = fuchsia::sysmem::PixelFormatType::R8G8B8A8;
    image_constraints.color_spaces_count = 1;
    image_constraints.color_space[0].type = fuchsia::sysmem::ColorSpaceType::SRGB;
    image_constraints.pixel_format.has_format_modifier = true;
    image_constraints.pixel_format.format_modifier.value =
        fuchsia::sysmem::FORMAT_MODIFIER_ARM_AFBC_16X16_YUV_TILED_HEADER;

    status = buffer_collection->SetConstraints(true, constraints);
    FX_CHECK(status == ZX_OK);

    zx_status_t allocation_status;
    fuchsia::sysmem::BufferCollectionInfo_2 buffer_collection_info = {};
    status =
        buffer_collection->WaitForBuffersAllocated(&allocation_status, &buffer_collection_info);
    FX_CHECK(status == ZX_OK);
    FX_CHECK(allocation_status == ZX_OK);
    FX_CHECK(buffer_collection_info.settings.image_format_constraints.pixel_format.type ==
             image_constraints.pixel_format.type);

    VkBufferCollectionBufferCreateInfoFUCHSIA collection_buffer_create_info = {
        .sType = VK_STRUCTURE_TYPE_BUFFER_COLLECTION_BUFFER_CREATE_INFO_FUCHSIA,
        .pNext = nullptr,
        .collection = collection_for_buffer,
        .index = 0,
    };
    buffer_create_info.pNext = &collection_buffer_create_info;
    VkBuffer buffer;
    result = dispatch_->CreateBuffer(device_, &buffer_create_info, pAllocator, &buffer);
    if (result != VK_SUCCESS) {
      return result;
    }

    auto cleanup_buffer = fbl::MakeAutoCall(
        [this, buffer, pAllocator] { dispatch_->DestroyBuffer(device_, buffer, pAllocator); });

    // Create memory allocation by importing the sysmem collection.
    VkImportMemoryBufferCollectionFUCHSIA import_info = {
        .sType = VK_STRUCTURE_TYPE_IMPORT_MEMORY_BUFFER_COLLECTION_FUCHSIA,
        .pNext = nullptr,
        .collection = collection_for_buffer,
        .index = 0,
    };
    VkDeviceMemory buffer_memory;
    VkDeviceAddress buffer_device_address;
    result = AllocateAndBindBufferMemory(buffer, &import_info, pAllocator, &buffer_memory,
                                         &buffer_device_address);
    if (result != VK_SUCCESS) {
      return result;
    }

    auto cleanup_buffer_memory = fbl::MakeAutoCall([this, buffer_memory, pAllocator] {
      dispatch_->FreeMemory(device_, buffer_memory, pAllocator);
    });

    // Create 4 byte auxiliary buffer.
    VkBuffer aux_buffer;
    result = CreateBuffer(4, nullptr, pAllocator, &aux_buffer);
    if (result != VK_SUCCESS) {
      return result;
    }

    auto cleanup_aux_buffer = fbl::MakeAutoCall([this, aux_buffer, pAllocator] {
      dispatch_->DestroyBuffer(device_, aux_buffer, pAllocator);
    });

    VkDeviceMemory aux_buffer_memory;
    VkDeviceAddress aux_buffer_device_address;
    result = AllocateAndBindBufferMemory(aux_buffer, nullptr, pAllocator, &aux_buffer_memory,
                                         &aux_buffer_device_address);
    if (result != VK_SUCCESS) {
      return result;
    }

    auto cleanup_aux_buffer_memory = fbl::MakeAutoCall([this, aux_buffer_memory, pAllocator] {
      dispatch_->FreeMemory(device_, aux_buffer_memory, pAllocator);
    });

    // Create image after successfully initializing extra state.
    VkBufferCollectionImageCreateInfoFUCHSIA collection_image_create_info = {
        .sType = VK_STRUCTURE_TYPE_BUFFER_COLLECTION_IMAGE_CREATE_INFO_FUCHSIA,
        .pNext = nullptr,
        .collection = collection,
        .index = 0,
    };
    VkImageCreateInfo image_create_info = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
        .pNext = &collection_image_create_info,
        .flags = pCreateInfo->flags & ~VK_IMAGE_CREATE_COMPACT_BIT_FUCHSIA,
        .imageType = pCreateInfo->imageType,
        .format = pCreateInfo->format,
        .extent = pCreateInfo->extent,
        .mipLevels = pCreateInfo->mipLevels,
        .arrayLayers = pCreateInfo->arrayLayers,
        .samples = pCreateInfo->samples,
        .tiling = pCreateInfo->tiling,
        .usage = pCreateInfo->usage,
        .sharingMode = pCreateInfo->sharingMode,
        .initialLayout = pCreateInfo->initialLayout,
    };
    result = dispatch_->CreateImage(device_, &image_create_info, pAllocator, pImage);
    if (result != VK_SUCCESS) {
      return result;
    }

    // Buffer collection can be closed as vulkan handle is enough.
    buffer_collection->Close();

    // Reset cleanup handlers.
    cleanup_collection.cancel();
    cleanup_buffer.cancel();
    cleanup_buffer_memory.cancel();
    cleanup_aux_buffer.cancel();
    cleanup_aux_buffer_memory.cancel();

    compact_images_.insert(
        {*pImage, CompactImage{
                      .collection = collection,
                      .buffer = Buffer{.buffer = buffer,
                                       .memory = buffer_memory,
                                       .device_address = buffer_device_address},
                      .aux = Buffer{.buffer = aux_buffer,
                                    .memory = aux_buffer_memory,
                                    .device_address = aux_buffer_device_address},
                      .allocation_size = buffer_collection_info.settings.buffer_settings.size_bytes,
                      .width_in_superblocks = width_in_superblocks,
                      .height_in_superblocks = height_in_superblocks,
                      .compact_memory_bound = false,
                  }});
    return result;
  }

  void DestroyImage(VkImage image, const VkAllocationCallbacks* pAllocator) {
    auto it = compact_images_.find(image);
    if (it != compact_images_.end()) {
      dispatch_->DestroyBuffer(device_, it->second.buffer.buffer, pAllocator);
      dispatch_->FreeMemory(device_, it->second.buffer.memory, pAllocator);
      dispatch_->DestroyBuffer(device_, it->second.aux.buffer, pAllocator);
      dispatch_->FreeMemory(device_, it->second.aux.memory, pAllocator);
      dispatch_->DestroyBufferCollectionFUCHSIA(device_, it->second.collection, pAllocator);
      compact_images_.erase(it);
    }
    dispatch_->DestroyImage(device_, image, pAllocator);
  }

  void GetImageMemoryRequirements2(const VkImageMemoryRequirementsInfo2* pInfo,
                                   VkMemoryRequirements2* pMemoryRequirements) {
    dispatch_->GetImageMemoryRequirements2(device_, pInfo, pMemoryRequirements);

    auto it = compact_images_.find(pInfo->image);
    if (it != compact_images_.end()) {
      VkBufferCollectionPropertiesFUCHSIA properties;
      dispatch_->GetBufferCollectionPropertiesFUCHSIA(device_, it->second.collection, &properties);
      pMemoryRequirements->memoryRequirements.memoryTypeBits &= properties.memoryTypeBits;
      auto dedicated_requirements = static_cast<VkMemoryDedicatedRequirements*>(
          vk_find_struct(pMemoryRequirements, MEMORY_DEDICATED_REQUIREMENTS));
      // Add dedicated allocation preference as required for compact images.
      if (dedicated_requirements) {
        dedicated_requirements->prefersDedicatedAllocation = VK_TRUE;
      }
    }
  }

  VkResult AllocateMemory(const VkMemoryAllocateInfo* pAllocateInfo,
                          const VkAllocationCallbacks* pAllocator, VkDeviceMemory* pMemory) {
    auto dedicated_alloc_info = static_cast<const VkMemoryDedicatedAllocateInfo*>(
        vk_find_struct_const(pAllocateInfo, MEMORY_DEDICATED_ALLOCATE_INFO));

    // Early out if not a dedicated allocation.
    if (!dedicated_alloc_info) {
      return dispatch_->AllocateMemory(device_, pAllocateInfo, pAllocator, pMemory);
    }

    // Early out if dedicated image is not compact.
    auto it = compact_images_.find(dedicated_alloc_info->image);
    if (it == compact_images_.end()) {
      return dispatch_->AllocateMemory(device_, pAllocateInfo, pAllocator, pMemory);
    }

    // Create memory allocation by importing the sysmem collection.
    VkImportMemoryBufferCollectionFUCHSIA import_info = {
        .sType = VK_STRUCTURE_TYPE_IMPORT_MEMORY_BUFFER_COLLECTION_FUCHSIA,
        .pNext = nullptr,
        .collection = it->second.collection,
        .index = 0,
    };
    VkMemoryAllocateInfo allocation_info = {
        .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .pNext = &import_info,
        .allocationSize = pAllocateInfo->allocationSize,
        .memoryTypeIndex = pAllocateInfo->memoryTypeIndex,
    };
    VkResult result = dispatch_->AllocateMemory(device_, &allocation_info, pAllocator, pMemory);
    if (result != VK_SUCCESS) {
      return result;
    }

    dedicated_image_memory_.insert({*pMemory, dedicated_alloc_info->image});
    return VK_SUCCESS;
  }

  void FreeMemory(VkDeviceMemory memory, const VkAllocationCallbacks* pAllocator) {
    dispatch_->FreeMemory(device_, memory, pAllocator);
    dedicated_image_memory_.erase(memory);
  }

  VkResult BindImageMemory(VkImage image, VkDeviceMemory memory, VkDeviceSize memoryOffset) {
    VkResult result = dispatch_->BindImageMemory(device_, image, memory, memoryOffset);
    if (result != VK_SUCCESS) {
      return result;
    }

    // Activate packing for compact image if bound to buffer collection
    // backed memory.
    auto it = compact_images_.find(image);
    if (it != compact_images_.end()) {
      it->second.compact_memory_bound = IsDedicatedImageMemory(memory, image);
    }

    return VK_SUCCESS;
  }

  VkResult BindImageMemory2(uint32_t bindInfoCount, const VkBindImageMemoryInfo* pBindInfos) {
    VkResult result = dispatch_->BindImageMemory2(device_, bindInfoCount, pBindInfos);
    if (result != VK_SUCCESS) {
      return result;
    }

    for (uint32_t i = 0; i < bindInfoCount; ++i) {
      // Activate packing for compact image if bound to buffer collection
      // backed memory.
      auto it = compact_images_.find(pBindInfos[i].image);
      if (it != compact_images_.end()) {
        it->second.compact_memory_bound =
            IsDedicatedImageMemory(pBindInfos[i].memory, pBindInfos[i].image);
      }
    }

    return VK_SUCCESS;
  }

  VkResult BeginCommandBuffer(VkCommandBuffer commandBuffer,
                              const VkCommandBufferBeginInfo* pBeginInfo) {
    command_buffer_state_[commandBuffer] = CommandBufferState();
    return dispatch_->BeginCommandBuffer(commandBuffer, pBeginInfo);
  }

  VkResult EndCommandBuffer(VkCommandBuffer commandBuffer) {
    command_buffer_state_.erase(commandBuffer);
    return dispatch_->EndCommandBuffer(commandBuffer);
  }

  void CmdBindPipeline(VkCommandBuffer commandBuffer, VkPipelineBindPoint pipelineBindPoint,
                       VkPipeline pipeline) {
    if (pipelineBindPoint == VK_PIPELINE_BIND_POINT_COMPUTE) {
      command_buffer_state_[commandBuffer].pipeline = pipeline;
    }
    dispatch_->CmdBindPipeline(commandBuffer, pipelineBindPoint, pipeline);
  }

  void CmdPushConstants(VkCommandBuffer commandBuffer, VkPipelineLayout layout,
                        VkShaderStageFlags stageFlags, uint32_t offset, uint32_t size,
                        const void* pValues) {
    auto& state = command_buffer_state_[commandBuffer];
    state.pipeline_layout = layout;
    state.stage_flags |= stageFlags;
    state.push_constants.resize(offset + size, 0);
    memcpy(state.push_constants.data() + offset, pValues, size);
    dispatch_->CmdPushConstants(commandBuffer, layout, stageFlags, offset, size, pValues);
  }

  void CmdPipelineBarrier(VkCommandBuffer commandBuffer, VkPipelineStageFlags srcStageMask,
                          VkPipelineStageFlags dstStageMask, VkDependencyFlags dependencyFlags,
                          uint32_t memoryBarrierCount, const VkMemoryBarrier* pMemoryBarriers,
                          uint32_t bufferMemoryBarrierCount,
                          const VkBufferMemoryBarrier* pBufferMemoryBarriers,
                          uint32_t imageMemoryBarrierCount,
                          const VkImageMemoryBarrier* pImageMemoryBarriers) {
    std::vector<VkImageMemoryBarrier> pack_image_memory_barriers;
    std::vector<VkImageMemoryBarrier> unpack_image_memory_barriers;
    std::vector<VkImageMemoryBarrier> other_image_memory_barriers;

    // Iterate over image barriers and extract barriers that required packing.
    for (uint32_t i = 0; i < imageMemoryBarrierCount; i++) {
      bool oldLayoutIsCompact = IsCompactLayout(pImageMemoryBarriers[i].oldLayout);
      bool newLayoutIsCompact = IsCompactLayout(pImageMemoryBarriers[i].newLayout);
      if (oldLayoutIsCompact != newLayoutIsCompact &&
          IsCompactImage(pImageMemoryBarriers[i].image)) {
        if (newLayoutIsCompact) {
          pack_image_memory_barriers.push_back(pImageMemoryBarriers[i]);
        } else {
          unpack_image_memory_barriers.push_back(pImageMemoryBarriers[i]);
        }
      } else {
        other_image_memory_barriers.push_back(pImageMemoryBarriers[i]);
      }
    }

    // Forward barriers that don't require packing.
    dispatch_->CmdPipelineBarrier(commandBuffer, srcStageMask, dstStageMask, dependencyFlags,
                                  memoryBarrierCount, pMemoryBarriers, bufferMemoryBarrierCount,
                                  pBufferMemoryBarriers,
                                  static_cast<uint32_t>(other_image_memory_barriers.size()),
                                  other_image_memory_barriers.data());

    // Check if we have at least one image barrier that require packing.
    if (!pack_image_memory_barriers.empty() || !pack_image_memory_barriers.empty()) {
      auto& state = command_buffer_state_[commandBuffer];

      // Emit commands for image barriers that use pack shader.
      for (auto& barrier : pack_image_memory_barriers) {
        PackingPipelineBarrier(commandBuffer, srcStageMask, dstStageMask, dependencyFlags, barrier,
                               pack_pipeline_);
      }

      // Emit commands for image barriers that use unpack shader.
      for (auto& barrier : unpack_image_memory_barriers) {
        PackingPipelineBarrier(commandBuffer, srcStageMask, dstStageMask, dependencyFlags, barrier,
                               unpack_pipeline_);
      }

      // Restore compute bind point if needed.
      if (state.pipeline != VK_NULL_HANDLE) {
        dispatch_->CmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, state.pipeline);
      }

      // Restore push constants if needed.
      if (state.pipeline_layout != VK_NULL_HANDLE) {
        dispatch_->CmdPushConstants(commandBuffer, state.pipeline_layout, state.stage_flags, 0,
                                    static_cast<uint32_t>(state.push_constants.size()),
                                    state.push_constants.data());
      }
    }
  }

  void CmdWriteCompactImageMemorySize(VkCommandBuffer commandBuffer, VkImage image,
                                      VkImageLayout imageLayout, VkBuffer buffer,
                                      VkDeviceSize bufferOffset,
                                      const VkImageSubresourceLayers* subresourceLayers) {
    FX_CHECK(compact_images_.find(image) != compact_images_.end());
    auto& compact_image = compact_images_[image];

    // Compact image support is limited to single layer 2D images.
    FX_CHECK(subresourceLayers->aspectMask == VK_IMAGE_ASPECT_COLOR_BIT);
    FX_CHECK(subresourceLayers->mipLevel == 0);
    FX_CHECK(subresourceLayers->baseArrayLayer == 0);
    FX_CHECK(subresourceLayers->layerCount == 1);

    if (IsCompactLayout(imageLayout) && compact_image.compact_memory_bound) {
      VkBufferCopy region = {
          .srcOffset = 0,
          .dstOffset = bufferOffset,
          .size = 4,
      };
      dispatch_->CmdCopyBuffer(commandBuffer, compact_image.aux.buffer, buffer, 1, &region);
    } else {
      dispatch_->CmdFillBuffer(commandBuffer, buffer, bufferOffset, 4,
                               static_cast<uint32_t>(compact_image.allocation_size));
    }
  }

 private:
  struct Buffer {
    VkBuffer buffer = VK_NULL_HANDLE;
    VkDeviceMemory memory = VK_NULL_HANDLE;
    VkDeviceAddress device_address;
  };
  struct CompactImage {
    VkBufferCollectionFUCHSIA collection = VK_NULL_HANDLE;
    Buffer buffer;
    Buffer aux;
    VkDeviceSize allocation_size;
    uint32_t width_in_superblocks;
    uint32_t height_in_superblocks;
    bool compact_memory_bound;
  };
  struct CommandBufferState {
    VkPipeline pipeline = VK_NULL_HANDLE;
    VkPipelineLayout pipeline_layout = VK_NULL_HANDLE;
    VkShaderStageFlags stage_flags = 0;
    std::vector<uint8_t> push_constants;
  };

  static bool IsSupportedUsage(VkImageUsageFlags usage) {
    // TODO(reveman): Add VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT
    // after implementing render pass support.
    constexpr VkImageUsageFlags kSupportedUsage = VK_IMAGE_USAGE_TRANSFER_SRC_BIT |
                                                  VK_IMAGE_USAGE_TRANSFER_DST_BIT |
                                                  VK_IMAGE_USAGE_SAMPLED_BIT;
    return !(usage & ~kSupportedUsage);
  }

  static bool IsSupportedFormat(VkFormat format) {
    constexpr VkFormat kSupportedFormats[] = {
        VK_FORMAT_R8G8B8A8_UNORM,
        VK_FORMAT_R8G8B8A8_SRGB,
    };
    return std::find(std::begin(kSupportedFormats), std::end(kSupportedFormats), format) !=
           std::end(kSupportedFormats);
  }

  static bool IsCompactLayout(VkImageLayout layout) {
    constexpr VkImageLayout compact_layouts[] = {
        VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
    };
    return std::find(std::begin(compact_layouts), std::end(compact_layouts), layout) !=
           std::end(compact_layouts);
  }

  static uint32_t RoundUp(uint32_t value, uint32_t multiple) {
    return ((value + (multiple - 1)) / multiple) * multiple;
  }

  bool IsCompactImage(VkImage image) const {
    auto it = compact_images_.find(image);
    return it != compact_images_.end() && it->second.compact_memory_bound;
  }

  bool IsDedicatedImageMemory(VkDeviceMemory memory, VkImage image) const {
    auto it = dedicated_image_memory_.find(memory);
    if (it == dedicated_image_memory_.end()) {
      return false;
    }
    return it->second == image;
  }

  VkResult CreatePipelineLayout(uint32_t push_constant_block_size,
                                const VkAllocationCallbacks* pAllocator, VkPipelineLayout* layout) {
    VkPushConstantRange push_constant_range = {
        .stageFlags = 0,
        .offset = 0,
        .size = push_constant_block_size,
    };
    VkPipelineLayoutCreateInfo pipeline_layout_info = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
        .pNext = nullptr,
        .flags = 0,
        .setLayoutCount = 0,
        .pSetLayouts = nullptr,
        .pushConstantRangeCount = 1,
        .pPushConstantRanges = &push_constant_range,
    };
    return dispatch_->CreatePipelineLayout(device_, &pipeline_layout_info, pAllocator, layout);
  }

  VkResult CreateComputePipeline(const uint32_t* spv, unsigned int spv_len,
                                 VkPipelineLayout pipeline_layout,
                                 const VkAllocationCallbacks* pAllocator, VkPipeline* pipeline) {
    VkShaderModule module;
    VkShaderModuleCreateInfo module_info = {
        .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
        .pNext = nullptr,
        .flags = 0,
        .codeSize = spv_len,
        .pCode = spv,
    };
    VkResult result = dispatch_->CreateShaderModule(device_, &module_info, pAllocator, &module);
    if (result != VK_SUCCESS) {
      return result;
    }
    VkPipelineShaderStageCreateInfo stage_info = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
        .pNext = nullptr,
        .flags = 0,
        .stage = VK_SHADER_STAGE_COMPUTE_BIT,
        .module = module,
        .pName = "main",
        .pSpecializationInfo = nullptr,
    };
    VkComputePipelineCreateInfo pipeline_info = {
        .sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,
        .pNext = nullptr,
        .flags = 0,
        .stage = stage_info,
        .layout = pipeline_layout,
        .basePipelineHandle = VK_NULL_HANDLE,
        .basePipelineIndex = 0,
    };
    result = dispatch_->CreateComputePipelines(device_, VK_NULL_HANDLE, 1, &pipeline_info,
                                               pAllocator, pipeline);
    dispatch_->DestroyShaderModule(device_, module, pAllocator);
    return result;
  }

  VkResult CreateBuffer(uint32_t size, const void* pNext, const VkAllocationCallbacks* pAllocator,
                        VkBuffer* pBuffer) {
    VkBufferCreateInfo buffer_create_info = {
        .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .pNext = pNext,
        .flags = 0,
        .size = size,
        .usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
    };
    return dispatch_->CreateBuffer(device_, &buffer_create_info, pAllocator, pBuffer);
  }

  VkResult AllocateAndBindBufferMemory(VkBuffer buffer, const void* pNext,
                                       const VkAllocationCallbacks* pAllocator,
                                       VkDeviceMemory* pMemory, VkDeviceAddress* pDeviceAddress) {
    VkMemoryRequirements memory_requirements;
    dispatch_->GetBufferMemoryRequirements(device_, buffer, &memory_requirements);

    uint32_t memory_type_index = __builtin_ctz(memory_requirements.memoryTypeBits);
    VkMemoryAllocateInfo allocate_info = {
        .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .pNext = pNext,
        .allocationSize = memory_requirements.size,
        .memoryTypeIndex = memory_type_index,
    };
    VkResult result = dispatch_->AllocateMemory(device_, &allocate_info, pAllocator, pMemory);
    if (result != VK_SUCCESS) {
      return result;
    }

    auto cleanup_memory = fbl::MakeAutoCall(
        [this, pMemory, pAllocator] { dispatch_->FreeMemory(device_, *pMemory, pAllocator); });

    result = dispatch_->BindBufferMemory(device_, buffer, *pMemory, 0);
    if (result != VK_SUCCESS) {
      return result;
    }

    VkBufferDeviceAddressInfo device_address_info = {
        .sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO,
        .pNext = nullptr,
        .buffer = buffer,
    };

    *pDeviceAddress = dispatch_->GetBufferDeviceAddress(device_, &device_address_info);

    cleanup_memory.cancel();
    return VK_SUCCESS;
  }

  void PackingPipelineBarrier(VkCommandBuffer commandBuffer, VkPipelineStageFlags srcStageMask,
                              VkPipelineStageFlags dstStageMask, VkDependencyFlags dependencyFlags,
                              const VkImageMemoryBarrier& barrier, VkPipeline pipeline) {
    auto& compact_image = compact_images_[barrier.image];

    // Image barrier to ensure that image memory is available to
    // compute shader.
    VkImageMemoryBarrier pre_image_barrier = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
        .pNext = nullptr,
        .srcAccessMask = barrier.srcAccessMask,
        .dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT,
        .oldLayout = barrier.oldLayout,
        .newLayout = VK_IMAGE_LAYOUT_GENERAL,
        .image = barrier.image,
        .subresourceRange = barrier.subresourceRange,
    };
    dispatch_->CmdPipelineBarrier(commandBuffer, srcStageMask, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                                  dependencyFlags | VK_DEPENDENCY_BY_REGION_BIT, 0, nullptr, 0,
                                  nullptr, 1, &pre_image_barrier);

    uint32_t num_superblocks =
        compact_image.width_in_superblocks * compact_image.height_in_superblocks;
    uint32_t body_offset =
        RoundUp(num_superblocks * kAfbcBytesPerSuperblockHeader, kAfbcBodyAlignment);

    // Bind pipeline used for this layout transition.
    dispatch_->CmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline);

    // Update push constants.
    PushConstantBlock push_constants = {
        .image_address = compact_image.buffer.device_address,
        .scratch_address = scratch_.device_address,
        .aux_address = compact_image.aux.device_address,
        .body_offset = body_offset,
        .block_count = num_superblocks,
    };
    dispatch_->CmdPushConstants(commandBuffer, pipeline_layout_, VK_SHADER_STAGE_COMPUTE_BIT, 0,
                                sizeof(push_constants), &push_constants);

    // Dispatch compute shader that performs the layout transition.
    // TODO(reveman): Use multiple workgroups for improved performance.
    dispatch_->CmdDispatch(commandBuffer, 1, 1, 1);

    // Image and buffer barriers to ensure that memory written by
    // compute shader is visible to destination stage.
    VkImageMemoryBarrier post_image_barrier = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
        .pNext = nullptr,
        .srcAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT,
        .dstAccessMask = barrier.dstAccessMask,
        .oldLayout = VK_IMAGE_LAYOUT_GENERAL,
        .newLayout = barrier.newLayout,
        .image = barrier.image,
        .subresourceRange = barrier.subresourceRange,
    };
    VkBufferMemoryBarrier post_buffer_barrier = {
        .sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER,
        .pNext = nullptr,
        .srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT,
        .dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .buffer = compact_image.aux.buffer,
        .offset = 0,
        .size = 4,
    };
    dispatch_->CmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, dstStageMask,
                                  dependencyFlags | VK_DEPENDENCY_BY_REGION_BIT, 0, nullptr, 1,
                                  &post_buffer_barrier, 1, &post_image_barrier);
  }

  VkDevice device_;
  VkLayerDispatchTable* dispatch_;
  fuchsia::sysmem::AllocatorSyncPtr sysmem_allocator_;
  Buffer scratch_;
  VkPipelineLayout pipeline_layout_ = VK_NULL_HANDLE;
  VkPipeline pack_pipeline_ = VK_NULL_HANDLE;
  VkPipeline unpack_pipeline_ = VK_NULL_HANDLE;
  std::unordered_map<VkImage, CompactImage> compact_images_;
  std::unordered_map<VkDeviceMemory, VkImage> dedicated_image_memory_;
  std::unordered_map<VkCommandBuffer, CommandBufferState> command_buffer_state_;
};

struct LayerData {
  VkInstance instance;
  std::unique_ptr<VkLayerDispatchTable> device_dispatch_table;
  std::unique_ptr<VkLayerInstanceDispatchTable> instance_dispatch_table;
  std::unique_ptr<ImageCompactor> compactor;
};

small_unordered_map<void*, LayerData*, 2> layer_data_map;

VKAPI_ATTR VkResult VKAPI_CALL CreateInstance(const VkInstanceCreateInfo* pCreateInfo,
                                              const VkAllocationCallbacks* pAllocator,
                                              VkInstance* pInstance) {
  VkLayerInstanceCreateInfo* chain_info = get_chain_info(pCreateInfo, VK_LAYER_LINK_INFO);

  FX_CHECK(chain_info->u.pLayerInfo);
  PFN_vkGetInstanceProcAddr fpGetInstanceProcAddr =
      chain_info->u.pLayerInfo->pfnNextGetInstanceProcAddr;
  PFN_vkCreateInstance fpCreateInstance =
      reinterpret_cast<PFN_vkCreateInstance>(fpGetInstanceProcAddr(nullptr, "vkCreateInstance"));
  if (!fpCreateInstance) {
    return VK_ERROR_INITIALIZATION_FAILED;
  }

  // Advance the link info for the next element on the chain.
  chain_info->u.pLayerInfo = chain_info->u.pLayerInfo->pNext;

  VkResult result = fpCreateInstance(pCreateInfo, pAllocator, pInstance);
  if (result != VK_SUCCESS) {
    return result;
  }

  LayerData* instance_layer_data = GetLayerDataPtr(get_dispatch_key(*pInstance), layer_data_map);
  instance_layer_data->instance = *pInstance;
  instance_layer_data->instance_dispatch_table = std::make_unique<VkLayerInstanceDispatchTable>();
  layer_init_instance_dispatch_table(*pInstance, instance_layer_data->instance_dispatch_table.get(),
                                     fpGetInstanceProcAddr);
  return result;
}

VKAPI_ATTR void VKAPI_CALL DestroyInstance(VkInstance instance,
                                           const VkAllocationCallbacks* pAllocator) {
  dispatch_key instance_key = get_dispatch_key(instance);
  LayerData* instance_layer_data = GetLayerDataPtr(instance_key, layer_data_map);

  instance_layer_data->instance_dispatch_table->DestroyInstance(instance, pAllocator);

  // Remove from |layer_data_map| and free LayerData struct.
  FreeLayerDataPtr(instance_key, layer_data_map);
}

VKAPI_ATTR VkResult VKAPI_CALL GetPhysicalDeviceImageFormatProperties(
    VkPhysicalDevice gpu, VkFormat format, VkImageType type, VkImageTiling tiling,
    VkImageUsageFlags usage, VkImageCreateFlags flags,
    VkImageFormatProperties* pImageFormatProperties) {
  void* gpu_key = get_dispatch_key(gpu);
  LayerData* gpu_layer_data = GetLayerDataPtr(gpu_key, layer_data_map);

  if (flags & VK_IMAGE_CREATE_COMPACT_BIT_FUCHSIA) {
    VkPhysicalDeviceProperties gpu_properties;
    gpu_layer_data->instance_dispatch_table->GetPhysicalDeviceProperties(gpu, &gpu_properties);
    return ImageCompactor::GetImageFormatProperties(gpu_properties, format, type, tiling, usage,
                                                    flags, pImageFormatProperties);
  }

  return gpu_layer_data->instance_dispatch_table->GetPhysicalDeviceImageFormatProperties(
      gpu, format, type, tiling, usage, flags, pImageFormatProperties);
}

VKAPI_ATTR VkResult VKAPI_CALL GetPhysicalDeviceImageFormatProperties2(
    VkPhysicalDevice gpu, const VkPhysicalDeviceImageFormatInfo2* pImageFormatInfo,
    VkImageFormatProperties2* pImageFormatProperties) {
  void* gpu_key = get_dispatch_key(gpu);
  LayerData* gpu_layer_data = GetLayerDataPtr(gpu_key, layer_data_map);

  if (pImageFormatInfo->flags & VK_IMAGE_CREATE_COMPACT_BIT_FUCHSIA) {
    VkPhysicalDeviceProperties gpu_properties;
    gpu_layer_data->instance_dispatch_table->GetPhysicalDeviceProperties(gpu, &gpu_properties);
    return ImageCompactor::GetImageFormatProperties(
        gpu_properties, pImageFormatInfo->format, pImageFormatInfo->type, pImageFormatInfo->tiling,
        pImageFormatInfo->usage, pImageFormatInfo->flags,
        &pImageFormatProperties->imageFormatProperties);
  }
  return gpu_layer_data->instance_dispatch_table->GetPhysicalDeviceImageFormatProperties2(
      gpu, pImageFormatInfo, pImageFormatProperties);
}

VKAPI_ATTR VkResult VKAPI_CALL CreateDevice(VkPhysicalDevice gpu,
                                            const VkDeviceCreateInfo* pCreateInfo,
                                            const VkAllocationCallbacks* pAllocator,
                                            VkDevice* pDevice) {
  void* gpu_key = get_dispatch_key(gpu);
  LayerData* gpu_layer_data = GetLayerDataPtr(gpu_key, layer_data_map);

  bool khr_buffer_device_address = false;
  bool fuchsia_buffer_collection_extension_available = false;
  uint32_t device_extension_count;
  VkResult result = gpu_layer_data->instance_dispatch_table->EnumerateDeviceExtensionProperties(
      gpu, nullptr, &device_extension_count, nullptr);
  if (result == VK_SUCCESS && device_extension_count > 0) {
    std::vector<VkExtensionProperties> device_extensions(device_extension_count);
    result = gpu_layer_data->instance_dispatch_table->EnumerateDeviceExtensionProperties(
        gpu, nullptr, &device_extension_count, device_extensions.data());
    if (result == VK_SUCCESS) {
      for (uint32_t i = 0; i < device_extension_count; i++) {
        if (!strcmp(VK_KHR_BUFFER_DEVICE_ADDRESS_EXTENSION_NAME,
                    device_extensions[i].extensionName)) {
          khr_buffer_device_address = true;
        }
        if (!strcmp(VK_FUCHSIA_BUFFER_COLLECTION_EXTENSION_NAME,
                    device_extensions[i].extensionName)) {
          fuchsia_buffer_collection_extension_available = true;
        }
      }
    }
  }
  if (!khr_buffer_device_address) {
    fprintf(stderr, "Device extension not available: %s\n",
            VK_KHR_BUFFER_DEVICE_ADDRESS_EXTENSION_NAME);
  }
  if (!fuchsia_buffer_collection_extension_available) {
    fprintf(stderr, "Device extension not available: %s\n",
            VK_FUCHSIA_BUFFER_COLLECTION_EXTENSION_NAME);
  }
  if (!khr_buffer_device_address || !fuchsia_buffer_collection_extension_available) {
    return VK_ERROR_INITIALIZATION_FAILED;
  }

  VkPhysicalDeviceProperties gpu_properties;
  gpu_layer_data->instance_dispatch_table->GetPhysicalDeviceProperties(gpu, &gpu_properties);

  VkDeviceCreateInfo create_info = *pCreateInfo;
  std::vector<const char*> enabled_extensions;
  for (uint32_t i = 0; i < create_info.enabledExtensionCount; i++) {
    enabled_extensions.push_back(create_info.ppEnabledExtensionNames[i]);
  }
  enabled_extensions.push_back(VK_KHR_BUFFER_DEVICE_ADDRESS_EXTENSION_NAME);
  enabled_extensions.push_back(VK_FUCHSIA_BUFFER_COLLECTION_EXTENSION_NAME);
  create_info.enabledExtensionCount = static_cast<uint32_t>(enabled_extensions.size());
  create_info.ppEnabledExtensionNames = enabled_extensions.data();

  VkLayerDeviceCreateInfo* chain_info = get_chain_info(pCreateInfo, VK_LAYER_LINK_INFO);

  FX_CHECK(chain_info->u.pLayerInfo);
  PFN_vkGetInstanceProcAddr fpGetInstanceProcAddr =
      chain_info->u.pLayerInfo->pfnNextGetInstanceProcAddr;
  PFN_vkGetDeviceProcAddr fpGetDeviceProcAddr = chain_info->u.pLayerInfo->pfnNextGetDeviceProcAddr;
  PFN_vkCreateDevice fpCreateDevice = reinterpret_cast<PFN_vkCreateDevice>(
      fpGetInstanceProcAddr(gpu_layer_data->instance, "vkCreateDevice"));
  if (!fpCreateDevice) {
    return VK_ERROR_INITIALIZATION_FAILED;
  }

  // Advance the link info for the next element on the chain.
  chain_info->u.pLayerInfo = chain_info->u.pLayerInfo->pNext;

  result = fpCreateDevice(gpu, &create_info, pAllocator, pDevice);
  if (result != VK_SUCCESS) {
    return result;
  }

  LayerData* device_layer_data = GetLayerDataPtr(get_dispatch_key(*pDevice), layer_data_map);

  // Setup device dispatch table.
  device_layer_data->device_dispatch_table = std::make_unique<VkLayerDispatchTable>();
  device_layer_data->instance = gpu_layer_data->instance;
  layer_init_device_dispatch_table(*pDevice, device_layer_data->device_dispatch_table.get(),
                                   fpGetDeviceProcAddr);

  // Create image compactor if GPU is supported.
  if (ImageCompactor::IsSupportedGPU(gpu_properties.vendorID, gpu_properties.deviceID)) {
    device_layer_data->compactor =
        std::make_unique<ImageCompactor>(*pDevice, device_layer_data->device_dispatch_table.get());
    return device_layer_data->compactor->Init(fpGetDeviceProcAddr, pAllocator);
  }

  return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL DestroyDevice(VkDevice device, const VkAllocationCallbacks* pAllocator) {
  dispatch_key device_key = get_dispatch_key(device);
  LayerData* device_layer_data = GetLayerDataPtr(device_key, layer_data_map);

  device_layer_data->device_dispatch_table->DestroyDevice(device, pAllocator);

  // Remove from |layer_data_map| and free LayerData struct.
  FreeLayerDataPtr(device_key, layer_data_map);
}

VKAPI_ATTR VkResult VKAPI_CALL CreateImage(VkDevice device, const VkImageCreateInfo* pCreateInfo,
                                           const VkAllocationCallbacks* pAllocator,
                                           VkImage* pImage) {
  dispatch_key device_key = get_dispatch_key(device);
  LayerData* device_layer_data = GetLayerDataPtr(device_key, layer_data_map);
  ImageCompactor* compactor = device_layer_data->compactor.get();

  FX_CHECK(compactor);

  return compactor->CreateImage(pCreateInfo, pAllocator, pImage);
}

VKAPI_ATTR void VKAPI_CALL DestroyImage(VkDevice device, VkImage image,
                                        const VkAllocationCallbacks* pAllocator) {
  dispatch_key device_key = get_dispatch_key(device);
  LayerData* device_layer_data = GetLayerDataPtr(device_key, layer_data_map);
  ImageCompactor* compactor = device_layer_data->compactor.get();

  FX_CHECK(compactor);

  compactor->DestroyImage(image, pAllocator);
}

VKAPI_ATTR void VKAPI_CALL GetImageMemoryRequirements2(VkDevice device,
                                                       const VkImageMemoryRequirementsInfo2* pInfo,
                                                       VkMemoryRequirements2* pMemoryRequirements) {
  dispatch_key device_key = get_dispatch_key(device);
  LayerData* device_layer_data = GetLayerDataPtr(device_key, layer_data_map);
  ImageCompactor* compactor = device_layer_data->compactor.get();

  FX_CHECK(compactor);

  compactor->GetImageMemoryRequirements2(pInfo, pMemoryRequirements);
}

VKAPI_ATTR VkResult VKAPI_CALL AllocateMemory(VkDevice device,
                                              const VkMemoryAllocateInfo* pAllocateInfo,
                                              const VkAllocationCallbacks* pAllocator,
                                              VkDeviceMemory* pMemory) {
  dispatch_key device_key = get_dispatch_key(device);
  LayerData* device_layer_data = GetLayerDataPtr(device_key, layer_data_map);
  ImageCompactor* compactor = device_layer_data->compactor.get();

  FX_CHECK(compactor);

  return compactor->AllocateMemory(pAllocateInfo, pAllocator, pMemory);
}

VKAPI_ATTR void VKAPI_CALL FreeMemory(VkDevice device, VkDeviceMemory memory,
                                      const VkAllocationCallbacks* pAllocator) {
  dispatch_key device_key = get_dispatch_key(device);
  LayerData* device_layer_data = GetLayerDataPtr(device_key, layer_data_map);
  ImageCompactor* compactor = device_layer_data->compactor.get();

  FX_CHECK(compactor);

  compactor->FreeMemory(memory, pAllocator);
}

VKAPI_ATTR VkResult VKAPI_CALL BindImageMemory(VkDevice device, VkImage image,
                                               VkDeviceMemory memory, VkDeviceSize memoryOffset) {
  dispatch_key device_key = get_dispatch_key(device);
  LayerData* device_layer_data = GetLayerDataPtr(device_key, layer_data_map);
  ImageCompactor* compactor = device_layer_data->compactor.get();

  FX_CHECK(compactor);

  return compactor->BindImageMemory(image, memory, memoryOffset);
}

VKAPI_ATTR VkResult VKAPI_CALL BindImageMemory2(VkDevice device, uint32_t bindInfoCount,
                                                const VkBindImageMemoryInfo* pBindInfos) {
  dispatch_key device_key = get_dispatch_key(device);
  LayerData* device_layer_data = GetLayerDataPtr(device_key, layer_data_map);
  ImageCompactor* compactor = device_layer_data->compactor.get();

  FX_CHECK(compactor);

  return compactor->BindImageMemory2(bindInfoCount, pBindInfos);
}

VKAPI_ATTR VkResult VKAPI_CALL BeginCommandBuffer(VkCommandBuffer commandBuffer,
                                                  const VkCommandBufferBeginInfo* pBeginInfo) {
  dispatch_key device_key = get_dispatch_key(commandBuffer);
  LayerData* device_layer_data = GetLayerDataPtr(device_key, layer_data_map);
  ImageCompactor* compactor = device_layer_data->compactor.get();

  FX_CHECK(compactor);

  return compactor->BeginCommandBuffer(commandBuffer, pBeginInfo);
}

VKAPI_ATTR VkResult VKAPI_CALL EndCommandBuffer(VkCommandBuffer commandBuffer) {
  dispatch_key device_key = get_dispatch_key(commandBuffer);
  LayerData* device_layer_data = GetLayerDataPtr(device_key, layer_data_map);
  ImageCompactor* compactor = device_layer_data->compactor.get();

  FX_CHECK(compactor);

  return compactor->EndCommandBuffer(commandBuffer);
}

VKAPI_ATTR void VKAPI_CALL CmdBindPipeline(VkCommandBuffer commandBuffer,
                                           VkPipelineBindPoint pipelineBindPoint,
                                           VkPipeline pipeline) {
  dispatch_key device_key = get_dispatch_key(commandBuffer);
  LayerData* device_layer_data = GetLayerDataPtr(device_key, layer_data_map);
  ImageCompactor* compactor = device_layer_data->compactor.get();

  FX_CHECK(compactor);

  return compactor->CmdBindPipeline(commandBuffer, pipelineBindPoint, pipeline);
}

VKAPI_ATTR void VKAPI_CALL CmdPushConstants(VkCommandBuffer commandBuffer, VkPipelineLayout layout,
                                            VkShaderStageFlags stageFlags, uint32_t offset,
                                            uint32_t size, const void* pValues) {
  dispatch_key device_key = get_dispatch_key(commandBuffer);
  LayerData* device_layer_data = GetLayerDataPtr(device_key, layer_data_map);
  ImageCompactor* compactor = device_layer_data->compactor.get();

  FX_CHECK(compactor);

  return compactor->CmdPushConstants(commandBuffer, layout, stageFlags, offset, size, pValues);
}

VKAPI_ATTR void VKAPI_CALL CmdPipelineBarrier(
    VkCommandBuffer commandBuffer, VkPipelineStageFlags srcStageMask,
    VkPipelineStageFlags dstStageMask, VkDependencyFlags dependencyFlags,
    uint32_t memoryBarrierCount, const VkMemoryBarrier* pMemoryBarriers,
    uint32_t bufferMemoryBarrierCount, const VkBufferMemoryBarrier* pBufferMemoryBarriers,
    uint32_t imageMemoryBarrierCount, const VkImageMemoryBarrier* pImageMemoryBarriers) {
  dispatch_key device_key = get_dispatch_key(commandBuffer);
  LayerData* device_layer_data = GetLayerDataPtr(device_key, layer_data_map);
  ImageCompactor* compactor = device_layer_data->compactor.get();

  FX_CHECK(compactor);

  compactor->CmdPipelineBarrier(commandBuffer, srcStageMask, dstStageMask, dependencyFlags,
                                memoryBarrierCount, pMemoryBarriers, bufferMemoryBarrierCount,
                                pBufferMemoryBarriers, imageMemoryBarrierCount,
                                pImageMemoryBarriers);
}

VKAPI_ATTR void VKAPI_CALL CmdWriteCompactImageMemorySizeFUCHSIA(
    VkCommandBuffer commandBuffer, VkImage image, VkImageLayout imageLayout, VkBuffer buffer,
    VkDeviceSize bufferOffset, const VkImageSubresourceLayers* subresourceLayers) {
  dispatch_key device_key = get_dispatch_key(commandBuffer);
  LayerData* device_layer_data = GetLayerDataPtr(device_key, layer_data_map);

  device_layer_data->compactor->CmdWriteCompactImageMemorySize(
      commandBuffer, image, imageLayout, buffer, bufferOffset, subresourceLayers);
}

VKAPI_ATTR VkResult VKAPI_CALL EnumerateInstanceLayerProperties(uint32_t* pCount,
                                                                VkLayerProperties* pProperties) {
  return util_GetLayerProperties(1, &compact_image_layer, pCount, pProperties);
}

VKAPI_ATTR VkResult VKAPI_CALL EnumerateDeviceLayerProperties(VkPhysicalDevice physicalDevice,
                                                              uint32_t* pCount,
                                                              VkLayerProperties* pProperties) {
  return util_GetLayerProperties(1, &compact_image_layer, pCount, pProperties);
}

VKAPI_ATTR VkResult VKAPI_CALL EnumerateInstanceExtensionProperties(
    const char* pLayerName, uint32_t* pCount, VkExtensionProperties* pProperties) {
  if (pLayerName && !strcmp(pLayerName, compact_image_layer.layerName)) {
    return util_GetExtensionProperties(0, nullptr, pCount, pProperties);
  }

  return VK_ERROR_LAYER_NOT_PRESENT;
}

VKAPI_ATTR VkResult VKAPI_CALL
EnumerateDeviceExtensionProperties(VkPhysicalDevice physicalDevice, const char* pLayerName,
                                   uint32_t* pCount, VkExtensionProperties* pProperties) {
  if (pLayerName && !strcmp(pLayerName, compact_image_layer.layerName)) {
    return util_GetExtensionProperties(ARRAY_SIZE(device_extensions), device_extensions, pCount,
                                       pProperties);
  }

  FX_CHECK(physicalDevice);

  dispatch_key key = get_dispatch_key(physicalDevice);
  LayerData* data = GetLayerDataPtr(key, layer_data_map);

  return data->instance_dispatch_table->EnumerateDeviceExtensionProperties(physicalDevice, nullptr,
                                                                           pCount, pProperties);
}

VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL GetDeviceProcAddr(VkDevice device, const char* funcName);
VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL GetInstanceProcAddr(VkInstance instance,
                                                             const char* funcName);

static inline PFN_vkVoidFunction layer_intercept_proc(const char* name) {
  if (!name || name[0] != 'v' || name[1] != 'k') {
    return nullptr;
  }
  name += 2;
  if (!strcmp(name, "GetDeviceProcAddr")) {
    return reinterpret_cast<PFN_vkVoidFunction>(GetDeviceProcAddr);
  }
  if (!strcmp(name, "CreateInstance")) {
    return reinterpret_cast<PFN_vkVoidFunction>(CreateInstance);
  }
  if (!strcmp(name, "DestroyInstance")) {
    return reinterpret_cast<PFN_vkVoidFunction>(DestroyInstance);
  }
  if (!strcmp(name, "CreateDevice")) {
    return reinterpret_cast<PFN_vkVoidFunction>(CreateDevice);
  }
  if (!strcmp(name, "DestroyDevice")) {
    return reinterpret_cast<PFN_vkVoidFunction>(DestroyDevice);
  }
  if (!strcmp(name, "CreateImage")) {
    return reinterpret_cast<PFN_vkVoidFunction>(CreateImage);
  }
  if (!strcmp(name, "DestroyImage")) {
    return reinterpret_cast<PFN_vkVoidFunction>(DestroyImage);
  }
  if (!strcmp(name, "GetImageMemoryRequirements2")) {
    return reinterpret_cast<PFN_vkVoidFunction>(GetImageMemoryRequirements2);
  }
  if (!strcmp(name, "AllocateMemory")) {
    return reinterpret_cast<PFN_vkVoidFunction>(AllocateMemory);
  }
  if (!strcmp(name, "FreeMemory")) {
    return reinterpret_cast<PFN_vkVoidFunction>(FreeMemory);
  }
  if (!strcmp(name, "BindImageMemory")) {
    return reinterpret_cast<PFN_vkVoidFunction>(BindImageMemory);
  }
  if (!strcmp(name, "BindImageMemory2")) {
    return reinterpret_cast<PFN_vkVoidFunction>(BindImageMemory2);
  }
  if (!strcmp(name, "BeginCommandBuffer")) {
    return reinterpret_cast<PFN_vkVoidFunction>(BeginCommandBuffer);
  }
  if (!strcmp(name, "EndCommandBuffer")) {
    return reinterpret_cast<PFN_vkVoidFunction>(EndCommandBuffer);
  }
  if (!strcmp(name, "CmdPipelineBarrier")) {
    return reinterpret_cast<PFN_vkVoidFunction>(CmdPipelineBarrier);
  }
  if (!strcmp(name, "CmdWriteCompactImageMemorySizeFUCHSIA")) {
    return reinterpret_cast<PFN_vkVoidFunction>(CmdWriteCompactImageMemorySizeFUCHSIA);
  }
  if (!strcmp("EnumerateDeviceExtensionProperties", name)) {
    return reinterpret_cast<PFN_vkVoidFunction>(EnumerateDeviceExtensionProperties);
  }
  if (!strcmp("EnumerateInstanceExtensionProperties", name)) {
    return reinterpret_cast<PFN_vkVoidFunction>(EnumerateInstanceExtensionProperties);
  }
  if (!strcmp("EnumerateDeviceLayerProperties", name)) {
    return reinterpret_cast<PFN_vkVoidFunction>(EnumerateDeviceLayerProperties);
  }
  if (!strcmp("EnumerateInstanceLayerProperties", name)) {
    return reinterpret_cast<PFN_vkVoidFunction>(EnumerateInstanceLayerProperties);
  }
  return nullptr;
}

static inline PFN_vkVoidFunction layer_intercept_instance_proc(const char* name) {
  if (!name || name[0] != 'v' || name[1] != 'k') {
    return nullptr;
  }
  name += 2;
  if (!strcmp(name, "GetInstanceProcAddr")) {
    return reinterpret_cast<PFN_vkVoidFunction>(GetInstanceProcAddr);
  }
  if (!strcmp(name, "CreateInstance")) {
    return reinterpret_cast<PFN_vkVoidFunction>(CreateInstance);
  }
  if (!strcmp(name, "DestroyInstance")) {
    return reinterpret_cast<PFN_vkVoidFunction>(DestroyInstance);
  }
  if (!strcmp("GetPhysicalDeviceImageFormatProperties", name)) {
    return reinterpret_cast<PFN_vkVoidFunction>(GetPhysicalDeviceImageFormatProperties);
  }
  if (!strcmp("GetPhysicalDeviceImageFormatProperties2", name)) {
    return reinterpret_cast<PFN_vkVoidFunction>(GetPhysicalDeviceImageFormatProperties2);
  }
  return nullptr;
}

VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL GetDeviceProcAddr(VkDevice device, const char* funcName) {
  PFN_vkVoidFunction addr;
  LayerData* data;

  FX_CHECK(device);

  data = GetLayerDataPtr(get_dispatch_key(device), layer_data_map);
  if (data->compactor) {
    addr = layer_intercept_proc(funcName);
    if (addr) {
      return addr;
    }
  }

  VkLayerDispatchTable* pTable = data->device_dispatch_table.get();

  if (!pTable->GetDeviceProcAddr) {
    return nullptr;
  }
  return pTable->GetDeviceProcAddr(device, funcName);
}

VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL GetInstanceProcAddr(VkInstance instance,
                                                             const char* funcName) {
  PFN_vkVoidFunction addr;
  LayerData* data;

  addr = layer_intercept_instance_proc(funcName);
  if (!addr) {
    addr = layer_intercept_proc(funcName);
  }
  if (addr) {
    return addr;
  }
  if (!instance) {
    return nullptr;
  }

  data = GetLayerDataPtr(get_dispatch_key(instance), layer_data_map);

  VkLayerInstanceDispatchTable* pTable = data->instance_dispatch_table.get();
  if (!pTable->GetInstanceProcAddr) {
    return nullptr;
  }
  addr = pTable->GetInstanceProcAddr(instance, funcName);
  return addr;
}

}  // namespace compact_image

VK_LAYER_EXPORT VKAPI_ATTR VkResult VKAPI_CALL vkEnumerateInstanceExtensionProperties(
    const char* pLayerName, uint32_t* pCount, VkExtensionProperties* pProperties) {
  return compact_image::EnumerateInstanceExtensionProperties(pLayerName, pCount, pProperties);
}

VK_LAYER_EXPORT VKAPI_ATTR VkResult VKAPI_CALL
vkEnumerateInstanceLayerProperties(uint32_t* pCount, VkLayerProperties* pProperties) {
  return compact_image::EnumerateInstanceLayerProperties(pCount, pProperties);
}

VK_LAYER_EXPORT VKAPI_ATTR VkResult VKAPI_CALL vkEnumerateDeviceLayerProperties(
    VkPhysicalDevice physicalDevice, uint32_t* pCount, VkLayerProperties* pProperties) {
  FX_CHECK(physicalDevice == VK_NULL_HANDLE);
  return compact_image::EnumerateDeviceLayerProperties(VK_NULL_HANDLE, pCount, pProperties);
}

VK_LAYER_EXPORT VKAPI_ATTR VkResult VKAPI_CALL
vkEnumerateDeviceExtensionProperties(VkPhysicalDevice physicalDevice, const char* pLayerName,
                                     uint32_t* pCount, VkExtensionProperties* pProperties) {
  FX_CHECK(physicalDevice == VK_NULL_HANDLE);
  return compact_image::EnumerateDeviceExtensionProperties(VK_NULL_HANDLE, pLayerName, pCount,
                                                           pProperties);
}

VK_LAYER_EXPORT VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL vkGetDeviceProcAddr(VkDevice dev,
                                                                             const char* funcName) {
  return compact_image::GetDeviceProcAddr(dev, funcName);
}

VK_LAYER_EXPORT VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL
vkGetInstanceProcAddr(VkInstance instance, const char* funcName) {
  return compact_image::GetInstanceProcAddr(instance, funcName);
}
