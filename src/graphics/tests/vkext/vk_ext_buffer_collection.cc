// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/fdio/directory.h>
#include <lib/zx/channel.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <vector>

#include <gtest/gtest.h>
#include <vulkan/vulkan.h>

#include "fuchsia/sysmem/cpp/fidl.h"
#include "src/graphics/tests/common/utils.h"
#include "src/graphics/tests/common/vulkan_context.h"

namespace {

VkImageCreateInfo GetDefaultImageCreateInfo(bool use_protected_memory, VkFormat format,
                                            uint32_t width, uint32_t height, bool linear) {
  VkImageCreateInfo image_create_info = {
      .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
      .pNext = nullptr,
      .flags = use_protected_memory ? VK_IMAGE_CREATE_PROTECTED_BIT : 0u,
      .imageType = VK_IMAGE_TYPE_2D,
      .format = format,
      .extent = VkExtent3D{width, height, 1},
      .mipLevels = 1,
      .arrayLayers = 1,
      .samples = VK_SAMPLE_COUNT_1_BIT,
      .tiling = linear ? VK_IMAGE_TILING_LINEAR : VK_IMAGE_TILING_OPTIMAL,
      // Only use sampled, because on Mali some other usages (like color attachment) aren't
      // supported for NV12, and some others (implementation-dependent) aren't supported with
      // AFBC.
      .usage = VK_IMAGE_USAGE_SAMPLED_BIT,
      .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
      .queueFamilyIndexCount = 0,      // not used since not sharing
      .pQueueFamilyIndices = nullptr,  // not used since not sharing
      .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
  };
  return image_create_info;
}

fuchsia::sysmem::ImageFormatConstraints GetDefaultSysmemImageFormatConstraints() {
  fuchsia::sysmem::ImageFormatConstraints bgra_image_constraints;
  bgra_image_constraints.required_min_coded_width = 1024;
  bgra_image_constraints.required_min_coded_height = 1024;
  bgra_image_constraints.required_max_coded_width = 1024;
  bgra_image_constraints.required_max_coded_height = 1024;
  bgra_image_constraints.max_coded_width = 8192;
  bgra_image_constraints.max_coded_height = 8192;
  bgra_image_constraints.max_bytes_per_row = 0xffffffff;
  bgra_image_constraints.pixel_format = {fuchsia::sysmem::PixelFormatType::BGRA32, false};
  bgra_image_constraints.color_spaces_count = 1;
  bgra_image_constraints.color_space[0].type = fuchsia::sysmem::ColorSpaceType::SRGB;
  return bgra_image_constraints;
}

class VulkanTest {
 public:
  ~VulkanTest();
  bool Initialize();
  bool Exec(VkFormat format, uint32_t width, uint32_t height, bool direct, bool linear,
            bool repeat_constraints_as_non_protected,
            const std::vector<fuchsia::sysmem::ImageFormatConstraints> &format_constraints =
                std::vector<fuchsia::sysmem::ImageFormatConstraints>());
  bool ExecBuffer(uint32_t size);

  void set_use_protected_memory(bool use) { use_protected_memory_ = use; }
  bool device_supports_protected_memory() const { return device_supports_protected_memory_; }

 private:
  bool InitVulkan();
  bool InitImage();
  bool InitFunctions();

  bool is_initialized_ = false;
  bool use_protected_memory_ = false;
  bool device_supports_protected_memory_ = false;
  std::unique_ptr<VulkanContext> ctx_;
  VkImage vk_image_{};
  VkDeviceMemory vk_device_memory_{};
  PFN_vkCreateBufferCollectionFUCHSIA vkCreateBufferCollectionFUCHSIA_;
  PFN_vkSetBufferCollectionConstraintsFUCHSIA vkSetBufferCollectionConstraintsFUCHSIA_;
  PFN_vkSetBufferCollectionBufferConstraintsFUCHSIA vkSetBufferCollectionBufferConstraintsFUCHSIA_;
  PFN_vkDestroyBufferCollectionFUCHSIA vkDestroyBufferCollectionFUCHSIA_;
  PFN_vkGetBufferCollectionPropertiesFUCHSIA vkGetBufferCollectionPropertiesFUCHSIA_;
};

VulkanTest::~VulkanTest() {
  const vk::Device &device = *ctx_->device();
  if (vk_image_) {
    vkDestroyImage(device, vk_image_, nullptr);
    vk_image_ = VK_NULL_HANDLE;
  }
  if (vk_device_memory_) {
    vkFreeMemory(device, vk_device_memory_, nullptr);
    vk_device_memory_ = VK_NULL_HANDLE;
  }
}

bool VulkanTest::Initialize() {
  if (is_initialized_) {
    return false;
  }

  if (!InitVulkan()) {
    RTN_MSG(false, "InitVulkan failed.\n");
  }

  is_initialized_ = true;

  return true;
}

bool VulkanTest::InitVulkan() {
  constexpr size_t kPhysicalDeviceIndex = 0;
  vk::ApplicationInfo app_info;
  app_info.pApplicationName = "vkext";
  app_info.apiVersion = VK_API_VERSION_1_1;
  vk::InstanceCreateInfo instance_info;
  instance_info.pApplicationInfo = &app_info;
  ctx_ = std::make_unique<VulkanContext>(kPhysicalDeviceIndex);
  ctx_->set_instance_info(instance_info);
  if (!ctx_->InitInstance()) {
    return false;
  }
  if (!ctx_->InitQueueFamily()) {
    return false;
  }

  // Set |device_supports_protected_memory_| flag.
  vk::PhysicalDeviceProtectedMemoryFeatures protected_memory(VK_TRUE);
  vk::PhysicalDeviceProperties physical_device_properties;
  ctx_->physical_device().getProperties(&physical_device_properties);
  if (VK_VERSION_MAJOR(physical_device_properties.apiVersion) != 1 ||
      VK_VERSION_MINOR(physical_device_properties.apiVersion) > 0) {
    vk::PhysicalDeviceFeatures2 features2;
    features2.pNext = &protected_memory;
    ctx_->physical_device().getFeatures2(&features2);
    if (protected_memory.protectedMemory) {
      device_supports_protected_memory_ = true;
    }
  }

  std::vector<const char *> enabled_device_extensions{VK_FUCHSIA_BUFFER_COLLECTION_EXTENSION_NAME};
  vk::DeviceCreateInfo device_info;
  device_info.pNext = device_supports_protected_memory_ ? &protected_memory : nullptr;
  device_info.pQueueCreateInfos = &ctx_->queue_info();
  device_info.queueCreateInfoCount = 1;
  device_info.enabledExtensionCount = static_cast<uint32_t>(enabled_device_extensions.size());
  device_info.ppEnabledExtensionNames = enabled_device_extensions.data();

  ctx_->set_device_info(device_info);
  if (!ctx_->InitDevice()) {
    return false;
  }

  if (!InitFunctions()) {
    return false;
  }

  return true;
}

bool VulkanTest::InitFunctions() {
  const vk::UniqueDevice &device = ctx_->device();
  vkCreateBufferCollectionFUCHSIA_ = reinterpret_cast<PFN_vkCreateBufferCollectionFUCHSIA>(
      device->getProcAddr("vkCreateBufferCollectionFUCHSIA"));
  if (!vkCreateBufferCollectionFUCHSIA_) {
    RTN_MSG(false, "No vkCreateBufferCollectionFUCHSIA");
  }

  vkDestroyBufferCollectionFUCHSIA_ = reinterpret_cast<PFN_vkDestroyBufferCollectionFUCHSIA>(
      device->getProcAddr("vkDestroyBufferCollectionFUCHSIA"));
  if (!vkDestroyBufferCollectionFUCHSIA_) {
    RTN_MSG(false, "No vkDestroyBufferCollectionFUCHSIA");
  }

  vkSetBufferCollectionConstraintsFUCHSIA_ =
      reinterpret_cast<PFN_vkSetBufferCollectionConstraintsFUCHSIA>(
          device->getProcAddr("vkSetBufferCollectionConstraintsFUCHSIA"));
  if (!vkSetBufferCollectionConstraintsFUCHSIA_) {
    RTN_MSG(false, "No vkSetBufferCollectionConstraintsFUCHSIA");
  }

  vkSetBufferCollectionBufferConstraintsFUCHSIA_ =
      reinterpret_cast<PFN_vkSetBufferCollectionBufferConstraintsFUCHSIA>(
          device->getProcAddr("vkSetBufferCollectionBufferConstraintsFUCHSIA"));
  if (!vkSetBufferCollectionBufferConstraintsFUCHSIA_) {
    RTN_MSG(false, "No vkSetBufferCollectionBufferConstraintsFUCHSIA");
  }

  vkGetBufferCollectionPropertiesFUCHSIA_ =
      reinterpret_cast<PFN_vkGetBufferCollectionPropertiesFUCHSIA>(
          device->getProcAddr("vkGetBufferCollectionPropertiesFUCHSIA"));

  if (!vkGetBufferCollectionPropertiesFUCHSIA_) {
    RTN_MSG(false, "No vkGetBufferCollectionPropertiesFUCHSIA_");
  }

  return true;
}

bool VulkanTest::Exec(
    VkFormat format, uint32_t width, uint32_t height, bool direct, bool linear,
    bool repeat_constraints_as_non_protected,
    const std::vector<fuchsia::sysmem::ImageFormatConstraints> &format_constraints) {
  const vk::Device &device = *ctx_->device();
  VkResult result;
  fuchsia::sysmem::AllocatorSyncPtr sysmem_allocator;
  zx_status_t status = fdio_service_connect("/svc/fuchsia.sysmem.Allocator",
                                            sysmem_allocator.NewRequest().TakeChannel().release());
  if (status != ZX_OK) {
    RTN_MSG(false, "Fdio_service_connect failed: %d\n", status);
  }
  fuchsia::sysmem::BufferCollectionTokenSyncPtr vulkan_token;
  status = sysmem_allocator->AllocateSharedCollection(vulkan_token.NewRequest());
  if (status != ZX_OK) {
    RTN_MSG(false, "AllocateSharedCollection failed: %d\n", status);
  }
  fuchsia::sysmem::BufferCollectionTokenSyncPtr local_token;

  status = vulkan_token->Duplicate(std::numeric_limits<uint32_t>::max(), local_token.NewRequest());
  if (status != ZX_OK) {
    RTN_MSG(false, "Duplicate failed: %d\n", status);
  }
  status = local_token->Sync();
  if (status != ZX_OK) {
    RTN_MSG(false, "Sync failed: %d\n", status);
  }

  // This bool suggests that we dup another token to set the same constraints, skipping protected
  // memory requirements. This emulates another participant which does not require protected memory.
  VkBufferCollectionFUCHSIA non_protected_collection;
  if (repeat_constraints_as_non_protected) {
    fuchsia::sysmem::BufferCollectionTokenSyncPtr repeat_token;
    status =
        vulkan_token->Duplicate(std::numeric_limits<uint32_t>::max(), repeat_token.NewRequest());
    if (status != ZX_OK) {
      RTN_MSG(false, "Duplicate failed: %d\n", status);
    }
    status = vulkan_token->Sync();
    if (status != ZX_OK) {
      RTN_MSG(false, "Sync failed: %d\n", status);
    }

    VkImageCreateInfo image_create_info =
        GetDefaultImageCreateInfo(/*use_protected_memory=*/false, format, width, height, linear);
    VkBufferCollectionCreateInfoFUCHSIA import_info = {
        .sType = VK_STRUCTURE_TYPE_BUFFER_COLLECTION_CREATE_INFO_FUCHSIA,
        .pNext = nullptr,
        .collectionToken = repeat_token.Unbind().TakeChannel().release(),
    };
    result =
        vkCreateBufferCollectionFUCHSIA_(device, &import_info, nullptr, &non_protected_collection);
    if (result != VK_SUCCESS) {
      RTN_MSG(false, "Failed to create buffer collection: %d\n", result);
    }
    result = vkSetBufferCollectionConstraintsFUCHSIA_(device, non_protected_collection,
                                                      &image_create_info);
    if (result != VK_SUCCESS) {
      RTN_MSG(false, "Failed to set buffer constraints: %d\n", result);
    }
  }

  VkImageCreateInfo image_create_info =
      GetDefaultImageCreateInfo(use_protected_memory_, format, width, height, linear);
  VkBufferCollectionCreateInfoFUCHSIA import_info = {
      .sType = VK_STRUCTURE_TYPE_BUFFER_COLLECTION_CREATE_INFO_FUCHSIA,
      .pNext = nullptr,
      .collectionToken = vulkan_token.Unbind().TakeChannel().release(),
  };
  VkBufferCollectionFUCHSIA collection;
  result = vkCreateBufferCollectionFUCHSIA_(device, &import_info, nullptr, &collection);
  if (result != VK_SUCCESS) {
    RTN_MSG(false, "Failed to create buffer collection: %d\n", result);
  }

  result = vkSetBufferCollectionConstraintsFUCHSIA_(device, collection, &image_create_info);

  if (result != VK_SUCCESS) {
    RTN_MSG(false, "Failed to set buffer constraints: %d\n", result);
  }

  fuchsia::sysmem::BufferCollectionSyncPtr sysmem_collection;
  status = sysmem_allocator->BindSharedCollection(std::move(local_token),
                                                  sysmem_collection.NewRequest());
  if (status != ZX_OK) {
    RTN_MSG(false, "BindSharedCollection failed: %d\n", status);
  }
  fuchsia::sysmem::BufferCollectionConstraints constraints{};
  if (!format_constraints.empty()) {
    // Use the other connection to specify the actual desired format and size,
    // which should be compatible with what the vulkan driver can use.
    assert(direct);
    constraints.usage.vulkan = fuchsia::sysmem::vulkanUsageTransferDst;
    // Try multiple format modifiers.
    constraints.image_format_constraints_count = format_constraints.size();
    for (uint32_t i = 0; i < constraints.image_format_constraints_count; i++) {
      constraints.image_format_constraints[i] = format_constraints[i];
    }
    status = sysmem_collection->SetConstraints(true, constraints);
  } else if (direct) {
    status = sysmem_collection->SetConstraints(false, constraints);
  } else {
    constraints.usage.vulkan = fuchsia::sysmem::vulkanUsageTransferDst;
    // The total buffer count should be 1 with or without this set (because
    // the Vulkan driver sets a minimum of one buffer).
    constraints.min_buffer_count_for_camping = 1;
    status = sysmem_collection->SetConstraints(true, constraints);
  }
  if (status != ZX_OK) {
    RTN_MSG(false, "SetConstraints failed: %d\n", status);
  }

  zx_status_t allocation_status;
  fuchsia::sysmem::BufferCollectionInfo_2 buffer_collection_info{};
  status = sysmem_collection->WaitForBuffersAllocated(&allocation_status, &buffer_collection_info);
  if (status != ZX_OK) {
    RTN_MSG(false, "WaitForBuffersAllocated failed: %d\n", status);
  }
  if (allocation_status != ZX_OK) {
    if (use_protected_memory_) {
      RTN_MSG(false, "WaitForBuffersAllocated failed: %d\n", allocation_status);
    }
    RTN_MSG(false, "WaitForBuffersAllocated failed: %d\n", allocation_status);
  }
  status = sysmem_collection->Close();
  if (status != ZX_OK) {
    RTN_MSG(false, "Close failed: %d\n", status);
  }

  EXPECT_EQ(1u, buffer_collection_info.buffer_count);
  fuchsia::sysmem::PixelFormat pixel_format =
      buffer_collection_info.settings.image_format_constraints.pixel_format;

  if (!direct) {
    fidl::Encoder encoder(fidl::Encoder::NO_HEADER);
    encoder.Alloc(fidl::EncodingInlineSize<fuchsia::sysmem::SingleBufferSettings>(&encoder));
    buffer_collection_info.settings.Encode(&encoder, 0);
    std::vector<uint8_t> encoded_data = encoder.TakeBytes();

    VkFuchsiaImageFormatFUCHSIA image_format_fuchsia = {
        .sType = VK_STRUCTURE_TYPE_FUCHSIA_IMAGE_FORMAT_FUCHSIA,
        .pNext = nullptr,
        .imageFormat = encoded_data.data(),
        .imageFormatSize = static_cast<uint32_t>(encoded_data.size())};
    image_create_info.pNext = &image_format_fuchsia;

    result = vkCreateImage(device, &image_create_info, nullptr, &vk_image_);
    if (result != VK_SUCCESS) {
      RTN_MSG(false, "vkCreateImage failed: %d\n", result);
    }
  } else {
    VkBufferCollectionImageCreateInfoFUCHSIA image_format_fuchsia = {
        .sType = VK_STRUCTURE_TYPE_BUFFER_COLLECTION_IMAGE_CREATE_INFO_FUCHSIA,
        .pNext = nullptr,
        .collection = collection,
        .index = 0};
    if (format == VK_FORMAT_UNDEFINED) {
      EXPECT_EQ(fuchsia::sysmem::PixelFormatType::BGRA32, pixel_format.type);
      // Ensure that the image created matches what was asked for on
      // sysmem_connection.
      image_create_info.extent.width = 1024;
      image_create_info.extent.height = 1024;
      image_create_info.format = VK_FORMAT_B8G8R8A8_UNORM;
    }
    image_create_info.pNext = &image_format_fuchsia;

    result = vkCreateImage(device, &image_create_info, nullptr, &vk_image_);
    if (result != VK_SUCCESS) {
      RTN_MSG(false, "vkCreateImage failed: %d\n", result);
    }
  }

  if (linear) {
    bool is_yuv = (format == VK_FORMAT_G8_B8R8_2PLANE_420_UNORM_KHR) ||
                  (format == VK_FORMAT_G8_B8_R8_3PLANE_420_UNORM_KHR);
    VkImageSubresource subresource = {
        .aspectMask = is_yuv ? VK_IMAGE_ASPECT_PLANE_0_BIT : VK_IMAGE_ASPECT_COLOR_BIT,
        .mipLevel = 0,
        .arrayLayer = 0};
    VkSubresourceLayout layout;
    vkGetImageSubresourceLayout(device, vk_image_, &subresource, &layout);

    VkDeviceSize min_bytes_per_pixel = is_yuv ? 1 : 4;
    EXPECT_LE(min_bytes_per_pixel * width, layout.rowPitch);
    EXPECT_LE(min_bytes_per_pixel * width * 64, layout.size);
  }

  if (linear && (format == VK_FORMAT_G8_B8_R8_3PLANE_420_UNORM_KHR)) {
    VkImageSubresource subresource = {
        .aspectMask = VK_IMAGE_ASPECT_PLANE_1_BIT, .mipLevel = 0, .arrayLayer = 0};
    VkSubresourceLayout b_layout;
    vkGetImageSubresourceLayout(device, vk_image_, &subresource, &b_layout);

    subresource.aspectMask = VK_IMAGE_ASPECT_PLANE_2_BIT;
    VkSubresourceLayout r_layout;
    vkGetImageSubresourceLayout(device, vk_image_, &subresource, &r_layout);

    // I420 has the U plane (mapped to B) before the V plane (mapped to R)
    EXPECT_LT(b_layout.offset, r_layout.offset);
  }

  if (!direct) {
    VkMemoryRequirements memory_reqs;
    vkGetImageMemoryRequirements(device, vk_image_, &memory_reqs);
    // Use first supported type
    uint32_t memory_type = __builtin_ctz(memory_reqs.memoryTypeBits);

    // The driver may not have the right information to choose the correct
    // heap for protected memory.
    EXPECT_FALSE(use_protected_memory_);

    VkImportMemoryZirconHandleInfoFUCHSIA handle_info = {
        .sType = VK_STRUCTURE_TYPE_TEMP_IMPORT_MEMORY_ZIRCON_HANDLE_INFO_FUCHSIA,
        .pNext = nullptr,
        VK_EXTERNAL_MEMORY_HANDLE_TYPE_TEMP_ZIRCON_VMO_BIT_FUCHSIA,
        buffer_collection_info.buffers[0].vmo.release()};

    VkMemoryAllocateInfo alloc_info = {
        .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .pNext = &handle_info,
        .allocationSize = memory_reqs.size,
        .memoryTypeIndex = memory_type,
    };

    if ((result = vkAllocateMemory(device, &alloc_info, nullptr, &vk_device_memory_)) !=
        VK_SUCCESS) {
      RTN_MSG(false, "vkAllocateMemory failed");
    }

    result = vkBindImageMemory(device, vk_image_, vk_device_memory_, 0);
    if (result != VK_SUCCESS) {
      RTN_MSG(false, "vkBindImageMemory failed");
    }
  } else {
    VkMemoryRequirements requirements;
    vkGetImageMemoryRequirements(device, vk_image_, &requirements);
    VkBufferCollectionPropertiesFUCHSIA properties = {
        .sType = VK_STRUCTURE_TYPE_BUFFER_COLLECTION_PROPERTIES_FUCHSIA};
    result = vkGetBufferCollectionPropertiesFUCHSIA_(device, collection, &properties);
    if (result != VK_SUCCESS) {
      RTN_MSG(false, "vkBindImageMemory failed");
    }

    EXPECT_EQ(1u, properties.count);
    uint32_t viable_memory_types = properties.memoryTypeBits & requirements.memoryTypeBits;
    EXPECT_NE(0u, viable_memory_types);
    uint32_t memory_type = __builtin_ctz(viable_memory_types);

    VkPhysicalDeviceMemoryProperties memory_properties;
    vkGetPhysicalDeviceMemoryProperties(ctx_->physical_device(), &memory_properties);

    EXPECT_LT(memory_type, memory_properties.memoryTypeCount);
    if (use_protected_memory_) {
      for (uint32_t i = 0; i < memory_properties.memoryTypeCount; ++i) {
        if (properties.memoryTypeBits & (1 << i)) {
          // Based only on the buffer collection it should be possible to
          // determine that this is protected memory. viable_memory_types
          // is a subset of these bits, so that should be true for it as
          // well.
          EXPECT_TRUE(memory_properties.memoryTypes[i].propertyFlags &
                      VK_MEMORY_PROPERTY_PROTECTED_BIT);
        }
      }
    } else {
      EXPECT_FALSE(memory_properties.memoryTypes[memory_type].propertyFlags &
                   VK_MEMORY_PROPERTY_PROTECTED_BIT);
    }

    VkImportMemoryBufferCollectionFUCHSIA import_info = {
        .sType = VK_STRUCTURE_TYPE_IMPORT_MEMORY_BUFFER_COLLECTION_FUCHSIA};

    import_info.collection = collection;
    import_info.index = 0;
    VkMemoryAllocateInfo alloc_info = {.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    alloc_info.pNext = &import_info;
    alloc_info.allocationSize = requirements.size;
    alloc_info.memoryTypeIndex = memory_type;

    result = vkAllocateMemory(device, &alloc_info, nullptr, &vk_device_memory_);
    if (result != VK_SUCCESS) {
      RTN_MSG(false, "vkCreateImage failed: %d\n", result);
    }

    result = vkBindImageMemory(device, vk_image_, vk_device_memory_, 0u);
    if (result != VK_SUCCESS) {
      RTN_MSG(false, "vkCreateImage failed: %d\n", result);
    }
  }

  vkDestroyBufferCollectionFUCHSIA_(device, collection, nullptr);
  if (repeat_constraints_as_non_protected) {
    vkDestroyBufferCollectionFUCHSIA_(device, non_protected_collection, nullptr);
  }

  return true;
}

bool VulkanTest::ExecBuffer(uint32_t size) {
  VkResult result;
  const vk::Device &device = *ctx_->device();
  fuchsia::sysmem::AllocatorSyncPtr sysmem_allocator;
  zx_status_t status = fdio_service_connect("/svc/fuchsia.sysmem.Allocator",
                                            sysmem_allocator.NewRequest().TakeChannel().release());
  if (status != ZX_OK) {
    RTN_MSG(false, "Fdio_service_connect failed: %d\n", status);
  }
  fuchsia::sysmem::BufferCollectionTokenSyncPtr vulkan_token;
  status = sysmem_allocator->AllocateSharedCollection(vulkan_token.NewRequest());
  if (status != ZX_OK) {
    RTN_MSG(false, "AllocateSharedCollection failed: %d\n", status);
  }
  fuchsia::sysmem::BufferCollectionTokenSyncPtr local_token;

  status = vulkan_token->Duplicate(std::numeric_limits<uint32_t>::max(), local_token.NewRequest());
  if (status != ZX_OK) {
    RTN_MSG(false, "Duplicate failed: %d\n", status);
  }
  status = local_token->Sync();
  if (status != ZX_OK) {
    RTN_MSG(false, "Sync failed: %d\n", status);
  }

  VkBufferCreateInfo buffer_create_info = {
      .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
      .pNext = nullptr,
      .flags = use_protected_memory_ ? VK_BUFFER_CREATE_PROTECTED_BIT : 0u,
      .size = size,
      .usage = VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
      .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
      .queueFamilyIndexCount = 0,
      .pQueueFamilyIndices = nullptr,
  };

  VkBufferCollectionCreateInfoFUCHSIA import_info = {
      .sType = VK_STRUCTURE_TYPE_BUFFER_COLLECTION_CREATE_INFO_FUCHSIA,
      .pNext = nullptr,
      .collectionToken = vulkan_token.Unbind().TakeChannel().release(),
  };
  VkBufferCollectionFUCHSIA collection;
  result = vkCreateBufferCollectionFUCHSIA_(device, &import_info, nullptr, &collection);
  if (result != VK_SUCCESS) {
    RTN_MSG(false, "Failed to import buffer collection: %d\n", result);
  }

  VkBufferConstraintsInfoFUCHSIA constraints = {
      .sType = VK_STRUCTURE_TYPE_BUFFER_CONSTRAINTS_INFO_FUCHSIA,
      .pNext = nullptr,
      .pBufferCreateInfo = &buffer_create_info,
      .requiredFormatFeatures = VK_FORMAT_FEATURE_VERTEX_BUFFER_BIT,
      .minCount = 2,
  };

  result = vkSetBufferCollectionBufferConstraintsFUCHSIA_(device, collection, &constraints);

  if (result != VK_SUCCESS) {
    RTN_MSG(false, "Failed to set buffer constraints: %d\n", result);
  }

  fuchsia::sysmem::BufferCollectionSyncPtr sysmem_collection;
  status = sysmem_allocator->BindSharedCollection(std::move(local_token),
                                                  sysmem_collection.NewRequest());
  if (status != ZX_OK) {
    RTN_MSG(false, "BindSharedCollection failed: %d\n", status);
  }
  fuchsia::sysmem::BufferCollectionConstraints sysmem_constraints{};
  status = sysmem_collection->SetConstraints(false, sysmem_constraints);
  if (status != ZX_OK) {
    RTN_MSG(false, "SetConstraints failed: %d\n", status);
  }

  zx_status_t allocation_status;
  fuchsia::sysmem::BufferCollectionInfo_2 buffer_collection_info{};
  status = sysmem_collection->WaitForBuffersAllocated(&allocation_status, &buffer_collection_info);
  if (status != ZX_OK) {
    RTN_MSG(false, "WaitForBuffersAllocated failed: %d\n", status);
  }
  if (allocation_status != ZX_OK) {
    RTN_MSG(false, "WaitForBuffersAllocated failed: %d\n", allocation_status);
  }
  status = sysmem_collection->Close();
  if (status != ZX_OK) {
    RTN_MSG(false, "Close failed: %d\n", status);
  }

  VkBufferCollectionBufferCreateInfoFUCHSIA collection_buffer_create_info = {
      .sType = VK_STRUCTURE_TYPE_BUFFER_COLLECTION_BUFFER_CREATE_INFO_FUCHSIA,
      .pNext = nullptr,
      .collection = collection,
      .index = 1};
  buffer_create_info.pNext = &collection_buffer_create_info;

  VkBuffer buffer;

  result = vkCreateBuffer(device, &buffer_create_info, nullptr, &buffer);
  if (result != VK_SUCCESS) {
    RTN_MSG(false, "vkCreateBuffer failed: %d\n", result);
  }

  VkMemoryRequirements requirements;
  vkGetBufferMemoryRequirements(device, buffer, &requirements);
  VkBufferCollectionPropertiesFUCHSIA properties = {
      .sType = VK_STRUCTURE_TYPE_BUFFER_COLLECTION_PROPERTIES_FUCHSIA};
  result = vkGetBufferCollectionPropertiesFUCHSIA_(device, collection, &properties);
  if (result != VK_SUCCESS) {
    RTN_MSG(false, "vkGetBufferCollectionProperties failed");
  }

  EXPECT_EQ(2u, properties.count);
  uint32_t viable_memory_types = properties.memoryTypeBits & requirements.memoryTypeBits;
  EXPECT_NE(0u, viable_memory_types);
  uint32_t memory_type = __builtin_ctz(viable_memory_types);
  VkPhysicalDeviceMemoryProperties memory_properties;
  vkGetPhysicalDeviceMemoryProperties(ctx_->physical_device(), &memory_properties);

  EXPECT_LT(memory_type, memory_properties.memoryTypeCount);
  if (use_protected_memory_) {
    for (uint32_t i = 0; i < memory_properties.memoryTypeCount; ++i) {
      if (properties.memoryTypeBits & (1 << i)) {
        // Based only on the buffer collection it should be possible to
        // determine that this is protected memory. viable_memory_types
        // is a subset of these bits, so that should be true for it as
        // well.
        EXPECT_TRUE(memory_properties.memoryTypes[i].propertyFlags &
                    VK_MEMORY_PROPERTY_PROTECTED_BIT);
      }
    }
  } else {
    EXPECT_FALSE(memory_properties.memoryTypes[memory_type].propertyFlags &
                 VK_MEMORY_PROPERTY_PROTECTED_BIT);
  }

  VkImportMemoryBufferCollectionFUCHSIA memory_import_info = {
      .sType = VK_STRUCTURE_TYPE_IMPORT_MEMORY_BUFFER_COLLECTION_FUCHSIA,
      .collection = collection,
      .index = 1};

  VkMemoryAllocateInfo alloc_info = {.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
  alloc_info.pNext = &memory_import_info;
  alloc_info.allocationSize = requirements.size;
  alloc_info.memoryTypeIndex = memory_type;

  result = vkAllocateMemory(device, &alloc_info, nullptr, &vk_device_memory_);
  if (result != VK_SUCCESS) {
    RTN_MSG(false, "vkBindBufferMemory failed: %d\n", result);
  }

  result = vkBindBufferMemory(device, buffer, vk_device_memory_, 0u);
  if (result != VK_SUCCESS) {
    RTN_MSG(false, "vkBindBufferMemory failed: %d\n", result);
  }

  vkDestroyBuffer(device, buffer, nullptr);

  vkDestroyBufferCollectionFUCHSIA_(device, collection, nullptr);

  return true;
}

// Parameter is true if the image should be linear.
class VulkanImageExtensionTest : public ::testing::TestWithParam<bool> {};

TEST_P(VulkanImageExtensionTest, BufferCollectionNV12) {
  VulkanTest test;
  ASSERT_TRUE(test.Initialize());
  ASSERT_TRUE(test.Exec(VK_FORMAT_G8_B8R8_2PLANE_420_UNORM, 64, 64, false, GetParam(), false));
}

TEST_P(VulkanImageExtensionTest, BufferCollectionI420) {
  VulkanTest test;
  ASSERT_TRUE(test.Initialize());
  ASSERT_TRUE(test.Exec(VK_FORMAT_G8_B8_R8_3PLANE_420_UNORM, 64, 64, false, GetParam(), false));
}

TEST_P(VulkanImageExtensionTest, BufferCollectionNV12_1025) {
  VulkanTest test;
  ASSERT_TRUE(test.Initialize());
  ASSERT_TRUE(test.Exec(VK_FORMAT_G8_B8R8_2PLANE_420_UNORM, 1025, 64, false, GetParam(), false));
}

TEST_P(VulkanImageExtensionTest, BufferCollectionRGBA) {
  VulkanTest test;
  ASSERT_TRUE(test.Initialize());
  ASSERT_TRUE(test.Exec(VK_FORMAT_R8G8B8A8_UNORM, 64, 64, false, GetParam(), false));
}

TEST_P(VulkanImageExtensionTest, BufferCollectionRGBA_1025) {
  VulkanTest test;
  ASSERT_TRUE(test.Initialize());
  ASSERT_TRUE(test.Exec(VK_FORMAT_R8G8B8A8_UNORM, 1025, 64, false, GetParam(), false));
}

TEST_P(VulkanImageExtensionTest, BufferCollectionDirectNV12) {
  VulkanTest test;
  ASSERT_TRUE(test.Initialize());
  ASSERT_TRUE(test.Exec(VK_FORMAT_G8_B8R8_2PLANE_420_UNORM, 64, 64, true, GetParam(), false));
}

TEST_P(VulkanImageExtensionTest, BufferCollectionDirectI420) {
  VulkanTest test;
  ASSERT_TRUE(test.Initialize());
  ASSERT_TRUE(test.Exec(VK_FORMAT_G8_B8_R8_3PLANE_420_UNORM, 64, 64, true, GetParam(), false));
}

TEST_P(VulkanImageExtensionTest, BufferCollectionDirectNV12_1280_546) {
  VulkanTest test;
  ASSERT_TRUE(test.Initialize());
  ASSERT_TRUE(test.Exec(VK_FORMAT_G8_B8R8_2PLANE_420_UNORM, 8192, 546, true, GetParam(), false));
}

TEST_P(VulkanImageExtensionTest, BufferCollectionUndefined) {
  VulkanTest test;
  ASSERT_TRUE(test.Initialize());

  fuchsia::sysmem::ImageFormatConstraints bgra_image_constraints =
      GetDefaultSysmemImageFormatConstraints();
  fuchsia::sysmem::ImageFormatConstraints bgra_tiled_image_constraints =
      GetDefaultSysmemImageFormatConstraints();
  bgra_tiled_image_constraints.pixel_format = {
      fuchsia::sysmem::PixelFormatType::BGRA32,
      true,
      {fuchsia::sysmem::FORMAT_MODIFIER_INTEL_I915_X_TILED}};
  std::vector<fuchsia::sysmem::ImageFormatConstraints> two_constraints{
      bgra_image_constraints, bgra_tiled_image_constraints};

  ASSERT_TRUE(test.Exec(VK_FORMAT_UNDEFINED, 64, 64, true, GetParam(), false, two_constraints));
}

TEST_P(VulkanImageExtensionTest, BufferCollectionMultipleFormats) {
  VulkanTest test;
  ASSERT_TRUE(test.Initialize());

  fuchsia::sysmem::ImageFormatConstraints nv12_image_constraints =
      GetDefaultSysmemImageFormatConstraints();
  nv12_image_constraints.pixel_format = {fuchsia::sysmem::PixelFormatType::NV12, false};
  nv12_image_constraints.color_space[0].type = fuchsia::sysmem::ColorSpaceType::REC709;
  fuchsia::sysmem::ImageFormatConstraints bgra_image_constraints =
      GetDefaultSysmemImageFormatConstraints();
  fuchsia::sysmem::ImageFormatConstraints bgra_tiled_image_constraints =
      GetDefaultSysmemImageFormatConstraints();
  bgra_tiled_image_constraints.pixel_format = {
      fuchsia::sysmem::PixelFormatType::BGRA32,
      true,
      {fuchsia::sysmem::FORMAT_MODIFIER_INTEL_I915_X_TILED}};
  std::vector<fuchsia::sysmem::ImageFormatConstraints> all_constraints{
      nv12_image_constraints, bgra_image_constraints, bgra_tiled_image_constraints};

  ASSERT_TRUE(test.Exec(VK_FORMAT_G8_B8R8_2PLANE_420_UNORM, 64, 64, true, GetParam(), false,
                        all_constraints));
  ASSERT_TRUE(
      test.Exec(VK_FORMAT_B8G8R8A8_UNORM, 64, 64, true, GetParam(), false, all_constraints));
}

TEST_P(VulkanImageExtensionTest, BufferCollectionProtectedRGBA) {
  VulkanTest test;
  test.set_use_protected_memory(true);
  ASSERT_TRUE(test.Initialize());
  ASSERT_TRUE(test.device_supports_protected_memory());
  ASSERT_TRUE(test.Exec(VK_FORMAT_R8G8B8A8_UNORM, 64, 64, true, GetParam(), false));
}

TEST_P(VulkanImageExtensionTest, ProtectedAndNonprotectedConstraints) {
  VulkanTest test;
  test.set_use_protected_memory(true);
  ASSERT_TRUE(test.Initialize());
  ASSERT_TRUE(test.device_supports_protected_memory());
  ASSERT_TRUE(test.Exec(VK_FORMAT_R8G8B8A8_UNORM, 64, 64, true, GetParam(), true));
}

INSTANTIATE_TEST_SUITE_P(, VulkanImageExtensionTest, ::testing::Bool());

TEST(VulkanExtensionTest, BufferCollectionBuffer1024) {
  VulkanTest test;
  ASSERT_TRUE(test.Initialize());
  ASSERT_TRUE(test.ExecBuffer(1024));
}

TEST(VulkanExtensionTest, BufferCollectionBuffer16384) {
  VulkanTest test;
  ASSERT_TRUE(test.Initialize());
  ASSERT_TRUE(test.ExecBuffer(16384));
}

TEST(VulkanExtensionTest, BufferCollectionProtectedBuffer) {
  VulkanTest test;
  test.set_use_protected_memory(true);
  ASSERT_TRUE(test.Initialize());
  ASSERT_TRUE(test.device_supports_protected_memory());
  ASSERT_TRUE(test.ExecBuffer(16384));
}

}  // namespace
