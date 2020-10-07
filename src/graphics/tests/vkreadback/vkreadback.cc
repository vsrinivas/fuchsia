// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "vkreadback.h"

#include "src/graphics/tests/common/utils.h"

namespace {

constexpr size_t kNumCommandBuffers = 1;

}  // namespace

#ifdef __Fuchsia__
#include <zircon/syscalls.h>
#endif

// Note, alignment must be a power of 2
template <class T>
static inline T round_up(T val, uint32_t alignment) {
  return ((val - 1) | (alignment - 1)) + 1;
}

VkReadbackTest::~VkReadbackTest() {
  if (image_initialized_) {
    vkFreeCommandBuffers(vk_device_, vk_command_pool_, kNumCommandBuffers, &vk_command_buffer_);
    vkDestroyCommandPool(vk_device_, vk_command_pool_, nullptr /* pAllocator */);
    if (VK_NULL_HANDLE != vk_device_memory_) {
      vkFreeMemory(vk_device_, vk_device_memory_, nullptr /* allocator */);
    }
    if (VK_NULL_HANDLE != vk_imported_device_memory_) {
      vkFreeMemory(vk_device_, vk_imported_device_memory_, nullptr /* allocator */);
    }
    vkDestroyImage(vk_device_, vk_image_, nullptr /* pAllocator */);
  }
  if (is_initialized_) {
    vkDeviceWaitIdle(vk_device_);
    vkDestroyDevice(vk_device_, nullptr /* pAllocator */);
    vkDestroyInstance(vk_instance_, nullptr /* pAllocator */);
  }
}

bool VkReadbackTest::Initialize() {
  if (is_initialized_) {
    return false;
  }

  if (!InitVulkan()) {
    RTN_MSG(false, "Failed to initialize Vulkan\n");
  }

  if (!InitImage()) {
    RTN_MSG(false, "InitImage failed\n");
  }

  is_initialized_ = true;

  return true;
}

bool VkReadbackTest::InitVulkan() {
  VkApplicationInfo app_info = {
      .sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
      .pNext = nullptr,
      .pApplicationName = "vkreadback",
      .applicationVersion = 0,
      .pEngineName = nullptr,
      .engineVersion = 0,
      .apiVersion = VK_API_VERSION_1_1,
  };

  VkInstanceCreateInfo create_info{
      VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,  // VkStructureType             sType;
      nullptr,                                 // const void*                 pNext;
      0,                                       // VkInstanceCreateFlags       flags;
      &app_info,                               // const VkApplicationInfo*    pApplicationInfo;
      0,                                       // uint32_t                    enabledLayerCount;
      nullptr,                                 // const char* const*          ppEnabledLayerNames;
      0,                                       // instance extensions count
      nullptr,                                 // instance extensions,
  };
  VkAllocationCallbacks* allocation_callbacks = nullptr;
  VkResult result;

  if ((result = vkCreateInstance(&create_info, allocation_callbacks, &vk_instance_)) !=
      VK_SUCCESS) {
    RTN_MSG(false, "vkCreateInstance failed %d\n", result);
  }

  uint32_t physical_device_count;
  if ((result = vkEnumeratePhysicalDevices(vk_instance_, &physical_device_count, nullptr)) !=
      VK_SUCCESS) {
    RTN_MSG(false, "vkEnumeratePhysicalDevices failed %d\n", result);
  }

  if (physical_device_count < 1) {
    RTN_MSG(false, "Unexpected physical_device_count %d\n", physical_device_count);
  }

  std::vector<VkPhysicalDevice> physical_devices(physical_device_count);
  if ((result = vkEnumeratePhysicalDevices(vk_instance_, &physical_device_count,
                                           physical_devices.data())) != VK_SUCCESS) {
    RTN_MSG(false, "vkEnumeratePhysicalDevices failed %d\n", result);
  }

  for (const auto& device : physical_devices) {
    VkPhysicalDeviceProperties properties;
    vkGetPhysicalDeviceProperties(device, &properties);

    if (ext_ == NONE) {
      continue;
    }

    if (VK_VERSION_MAJOR(properties.apiVersion) == 1 &&
        VK_VERSION_MINOR(properties.apiVersion) == 0) {
      printf("Skipping 1.1 checks");
      continue;
    }

    // Test external buffer/image capabilities
    VkPhysicalDeviceExternalBufferInfo buffer_info = {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_EXTERNAL_BUFFER_INFO,
        .flags = 0,
        .usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
        .handleType = VK_EXTERNAL_MEMORY_HANDLE_TYPE_TEMP_ZIRCON_VMO_BIT_FUCHSIA,
    };
    VkExternalBufferProperties buffer_props = {
        .sType = VK_STRUCTURE_TYPE_EXTERNAL_BUFFER_PROPERTIES,
        .pNext = nullptr,
    };
    vkGetPhysicalDeviceExternalBufferProperties(device, &buffer_info, &buffer_props);
    EXPECT_EQ(
        buffer_props.externalMemoryProperties.externalMemoryFeatures,
        0u | VK_EXTERNAL_MEMORY_FEATURE_EXPORTABLE_BIT | VK_EXTERNAL_MEMORY_FEATURE_IMPORTABLE_BIT);
    EXPECT_EQ(buffer_props.externalMemoryProperties.exportFromImportedHandleTypes,
              VK_EXTERNAL_MEMORY_HANDLE_TYPE_TEMP_ZIRCON_VMO_BIT_FUCHSIA);
    EXPECT_EQ(buffer_props.externalMemoryProperties.compatibleHandleTypes,
              VK_EXTERNAL_MEMORY_HANDLE_TYPE_TEMP_ZIRCON_VMO_BIT_FUCHSIA);

    VkPhysicalDeviceExternalImageFormatInfo ext_format_info = {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_EXTERNAL_IMAGE_FORMAT_INFO,
        .pNext = nullptr,
        .handleType = VK_EXTERNAL_MEMORY_HANDLE_TYPE_TEMP_ZIRCON_VMO_BIT_FUCHSIA,
    };
    VkPhysicalDeviceImageFormatInfo2 image_format_info = {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_IMAGE_FORMAT_INFO_2,
        .pNext = &ext_format_info,
        .format = VK_FORMAT_R8G8B8A8_UNORM,
        .type = VK_IMAGE_TYPE_2D,
        .tiling = VK_IMAGE_TILING_LINEAR,
        .usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
        .flags = 0u,
    };
    VkExternalImageFormatProperties ext_format_props = {
        .sType = VK_STRUCTURE_TYPE_EXTERNAL_IMAGE_FORMAT_PROPERTIES,
        .pNext = nullptr,
    };
    VkImageFormatProperties2 image_format_props = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_FORMAT_PROPERTIES_2,
        .pNext = &ext_format_props,
    };
    vkGetPhysicalDeviceImageFormatProperties2(device, &image_format_info, &image_format_props);
    EXPECT_EQ(
        ext_format_props.externalMemoryProperties.externalMemoryFeatures,
        0u | VK_EXTERNAL_MEMORY_FEATURE_EXPORTABLE_BIT | VK_EXTERNAL_MEMORY_FEATURE_IMPORTABLE_BIT);
    EXPECT_EQ(ext_format_props.externalMemoryProperties.exportFromImportedHandleTypes,
              VK_EXTERNAL_MEMORY_HANDLE_TYPE_TEMP_ZIRCON_VMO_BIT_FUCHSIA);
    EXPECT_EQ(ext_format_props.externalMemoryProperties.compatibleHandleTypes,
              VK_EXTERNAL_MEMORY_HANDLE_TYPE_TEMP_ZIRCON_VMO_BIT_FUCHSIA);
  }

  uint32_t queue_family_count;
  vkGetPhysicalDeviceQueueFamilyProperties(physical_devices[0], &queue_family_count, nullptr);

  if (queue_family_count < 1) {
    RTN_MSG(false, "Invalid queue_family_count %d\n", queue_family_count);
  }

  std::vector<VkQueueFamilyProperties> queue_family_properties(queue_family_count);
  vkGetPhysicalDeviceQueueFamilyProperties(physical_devices[0], &queue_family_count,
                                           queue_family_properties.data());

  int32_t queue_family_index = -1;
  for (uint32_t i = 0; i < queue_family_count; i++) {
    if (queue_family_properties[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) {
      queue_family_index = i;
      break;
    }
  }

  if (queue_family_index < 0) {
    RTN_MSG(false, "Couldn't find an appropriate queue\n");
  }

  constexpr float kQueuePriority = 0.0;

  VkDeviceQueueCreateInfo queue_create_info = {.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
                                               .pNext = nullptr,
                                               .flags = 0,
                                               .queueFamilyIndex = 0,
                                               .queueCount = 1,
                                               .pQueuePriorities = &kQueuePriority};

  std::vector<const char*> enabled_extension_names;
  switch (ext_) {
#ifdef __Fuchsia__
    case VK_FUCHSIA_EXTERNAL_MEMORY:
      enabled_extension_names.push_back(VK_FUCHSIA_EXTERNAL_MEMORY_EXTENSION_NAME);
      break;
#endif
    default:
      break;
  }

  VkDeviceCreateInfo createInfo = {
      .sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
      .pNext = nullptr,
      .flags = 0,
      .queueCreateInfoCount = 1,
      .pQueueCreateInfos = &queue_create_info,
      .enabledLayerCount = 0,
      .ppEnabledLayerNames = nullptr,
      .enabledExtensionCount = static_cast<uint32_t>(enabled_extension_names.size()),
      .ppEnabledExtensionNames = enabled_extension_names.data(),
      .pEnabledFeatures = nullptr};
  VkDevice vkdevice;

  if ((result = vkCreateDevice(physical_devices[0], &createInfo, nullptr /* allocationcallbacks */,
                               &vkdevice)) != VK_SUCCESS) {
    RTN_MSG(false, "vkCreateDevice failed: %d\n", result);
  }

  switch (ext_) {
#ifdef __Fuchsia__
    case VK_FUCHSIA_EXTERNAL_MEMORY:
      vkGetMemoryZirconHandleFUCHSIA_ = reinterpret_cast<PFN_vkGetMemoryZirconHandleFUCHSIA>(
          vkGetInstanceProcAddr(vk_instance_, "vkGetMemoryZirconHandleFUCHSIA"));
      if (!vkGetMemoryZirconHandleFUCHSIA_) {
        RTN_MSG(false, "Couldn't find vkGetMemoryZirconHandleFUCHSIA\n");
      }

      vkGetMemoryZirconHandlePropertiesFUCHSIA_ =
          reinterpret_cast<PFN_vkGetMemoryZirconHandlePropertiesFUCHSIA>(
              vkGetInstanceProcAddr(vk_instance_, "vkGetMemoryZirconHandlePropertiesFUCHSIA"));
      if (!vkGetMemoryZirconHandlePropertiesFUCHSIA_) {
        RTN_MSG(false, "Couldn't find vkGetMemoryZirconHandlePropertiesFUCHSIA\n");
      }
      break;
#endif

    default:
      break;
  }

  vk_physical_device_ = physical_devices[0];
  vk_device_ = vkdevice;

  vkGetDeviceQueue(vkdevice, queue_family_index, 0, &vk_queue_);

  return true;
}

bool VkReadbackTest::InitImage() {
  VkImageCreateInfo image_create_info = {
      .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
      .pNext = nullptr,
      .flags = VK_IMAGE_CREATE_MUTABLE_FORMAT_BIT,
      .imageType = VK_IMAGE_TYPE_2D,
      .format = VK_FORMAT_R8G8B8A8_UNORM,
      .extent = VkExtent3D{kWidth, kHeight, 1},
      .mipLevels = 1,
      .arrayLayers = 1,
      .samples = VK_SAMPLE_COUNT_1_BIT,
      .tiling = VK_IMAGE_TILING_LINEAR,
      .usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
      .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
      .queueFamilyIndexCount = 0,      // not used since not sharing
      .pQueueFamilyIndices = nullptr,  // not used since not sharing
      .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
  };

  VkResult result;

  if ((result = vkCreateImage(vk_device_, &image_create_info, nullptr, &vk_image_)) != VK_SUCCESS) {
    RTN_MSG(false, "vkCreateImage failed: %d\n", result);
  }

  VkMemoryRequirements memory_reqs;
  vkGetImageMemoryRequirements(vk_device_, vk_image_, &memory_reqs);
  // Add an offset to all operations that's correctly aligned and at least a
  // page in size, to ensure rounding the VMO down to a page offset will
  // cause it to point to a separate page.
  constexpr uint32_t kOffset = 128;
  bind_offset_ = getpagesize() + kOffset;
  if (memory_reqs.alignment) {
    bind_offset_ = round_up(bind_offset_, memory_reqs.alignment);
  }

  VkPhysicalDeviceMemoryProperties memory_props;
  vkGetPhysicalDeviceMemoryProperties(vk_physical_device_, &memory_props);

  uint32_t memory_type = 0;
  for (; memory_type < VK_MAX_MEMORY_TYPES; memory_type++) {
    if ((memory_reqs.memoryTypeBits & (1 << memory_type)) &&
        (memory_props.memoryTypes[memory_type].propertyFlags &
         VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT)) {
      break;
    }
  }
  if (memory_type >= VK_MAX_MEMORY_TYPES) {
    RTN_MSG(false, "Can't find compatible mappable memory for image\n");
  }

  VkExportMemoryAllocateInfoKHR export_info = {
      .sType = VK_STRUCTURE_TYPE_EXPORT_MEMORY_ALLOCATE_INFO_KHR,
      .pNext = nullptr,
      .handleTypes = VK_EXTERNAL_MEMORY_HANDLE_TYPE_TEMP_ZIRCON_VMO_BIT_FUCHSIA,
  };

  VkMemoryAllocateInfo alloc_info = {
      .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
      .pNext =
          (ext_ == VK_FUCHSIA_EXTERNAL_MEMORY && !device_memory_handle_) ? &export_info : nullptr,
      .allocationSize = memory_reqs.size + bind_offset_,
      .memoryTypeIndex = memory_type,
  };

  if ((result = vkAllocateMemory(vk_device_, &alloc_info, nullptr, &vk_device_memory_)) !=
      VK_SUCCESS) {
    RTN_MSG(false, "vkAllocateMemory failed\n");
  }

#ifdef __Fuchsia__
  if (ext_ == VK_FUCHSIA_EXTERNAL_MEMORY && device_memory_handle_) {
    size_t vmo_size;
    zx_vmo_get_size(device_memory_handle_, &vmo_size);

    VkMemoryZirconHandlePropertiesFUCHSIA properties{
        .sType = VK_STRUCTURE_TYPE_TEMP_MEMORY_ZIRCON_HANDLE_PROPERTIES_FUCHSIA,
        .pNext = nullptr,
    };
    result = vkGetMemoryZirconHandlePropertiesFUCHSIA_(
        vk_device_, VK_EXTERNAL_MEMORY_HANDLE_TYPE_TEMP_ZIRCON_VMO_BIT_FUCHSIA,
        device_memory_handle_, &properties);
    if (result != VK_SUCCESS) {
      RTN_MSG(false, "vkGetMemoryZirconHandlePropertiesFUCHSIA returned %d\n", result);
    }
    // Find index of lowest set bit.
    memory_type = __builtin_ctz(properties.memoryTypeBits);

    VkImportMemoryZirconHandleInfoFUCHSIA handle_info = {
        .sType = VK_STRUCTURE_TYPE_TEMP_IMPORT_MEMORY_ZIRCON_HANDLE_INFO_FUCHSIA,
        .pNext = nullptr,
        .handleType = VK_EXTERNAL_MEMORY_HANDLE_TYPE_TEMP_ZIRCON_VMO_BIT_FUCHSIA,
        .handle = device_memory_handle_};

    VkMemoryAllocateInfo info = {.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
                                 .pNext = &handle_info,
                                 .allocationSize = vmo_size,
                                 .memoryTypeIndex = memory_type};

    if ((result = vkAllocateMemory(vk_device_, &info, nullptr, &vk_imported_device_memory_)) !=
        VK_SUCCESS) {
      RTN_MSG(false, "vkAllocateMemory failed\n");
    }

  } else if (ext_ == VK_FUCHSIA_EXTERNAL_MEMORY) {
    uint32_t handle;
    VkMemoryGetZirconHandleInfoFUCHSIA get_handle_info = {
        .sType = VK_STRUCTURE_TYPE_TEMP_MEMORY_GET_ZIRCON_HANDLE_INFO_FUCHSIA,
        .pNext = nullptr,
        .memory = vk_device_memory_,
        .handleType = VK_EXTERNAL_MEMORY_HANDLE_TYPE_TEMP_ZIRCON_VMO_BIT_FUCHSIA};
    if ((result = vkGetMemoryZirconHandleFUCHSIA_(vk_device_, &get_handle_info, &handle)) !=
        VK_SUCCESS) {
      RTN_MSG(false, "vkGetMemoryZirconHandleFUCHSIA failed\n");
    }

    VkMemoryZirconHandlePropertiesFUCHSIA properties{
        .sType = VK_STRUCTURE_TYPE_TEMP_MEMORY_ZIRCON_HANDLE_PROPERTIES_FUCHSIA,
        .pNext = nullptr,
    };
    result = vkGetMemoryZirconHandlePropertiesFUCHSIA_(
        vk_device_, VK_EXTERNAL_MEMORY_HANDLE_TYPE_TEMP_ZIRCON_VMO_BIT_FUCHSIA, handle,
        &properties);
    if (result != VK_SUCCESS) {
      RTN_MSG(false, "vkGetMemoryZirconHandlePropertiesFUCHSIA returned %d\n", result);
    }

    device_memory_handle_ = handle;
  }
#endif

  void* addr;
  if ((result = vkMapMemory(vk_device_, vk_device_memory_, 0, VK_WHOLE_SIZE, 0, &addr)) !=
      VK_SUCCESS) {
    RTN_MSG(false, "vkMapMemory failed: %d\n", result);
  }

  constexpr int kFill = 0xab;
  memset(addr, kFill, memory_reqs.size + bind_offset_);

  vkUnmapMemory(vk_device_, vk_device_memory_);

  if ((result = vkBindImageMemory(vk_device_, vk_image_, vk_device_memory_, bind_offset_)) !=
      VK_SUCCESS) {
    RTN_MSG(false, "vkBindImageMemory failed\n");
  }

  VkCommandPoolCreateInfo command_pool_create_info = {
      .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
      .pNext = nullptr,
      .flags = 0,
      .queueFamilyIndex = 0,
  };
  if ((result = vkCreateCommandPool(vk_device_, &command_pool_create_info, nullptr,
                                    &vk_command_pool_)) != VK_SUCCESS) {
    RTN_MSG(false, "vkCreateCommandPool failed: %d\n", result);
  }

  VkCommandBufferAllocateInfo command_buffer_create_info = {
      .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
      .pNext = nullptr,
      .commandPool = vk_command_pool_,
      .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
      .commandBufferCount = kNumCommandBuffers};
  if ((result = vkAllocateCommandBuffers(vk_device_, &command_buffer_create_info,
                                         &vk_command_buffer_)) != VK_SUCCESS) {
    RTN_MSG(false, "vkAllocateCommandBuffers failed: %d\n", result);
  }

  VkCommandBufferBeginInfo begin_info = {
      .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
      .pNext = nullptr,
      .flags = 0,
      .pInheritanceInfo = nullptr,  // ignored for primary buffers
  };
  if ((result = vkBeginCommandBuffer(vk_command_buffer_, &begin_info)) != VK_SUCCESS) {
    RTN_MSG(false, "vkBeginCommandBuffer failed: %d\n", result);
  }

  VkClearColorValue color_value = {.float32 = {1.0f, 0.0f, 0.5f, 0.75f}};

  VkImageSubresourceRange image_subres_range = {
      .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
      .baseMipLevel = 0,
      .levelCount = 1,
      .baseArrayLayer = 0,
      .layerCount = 1,
  };

  vkCmdClearColorImage(vk_command_buffer_, vk_image_, VK_IMAGE_LAYOUT_GENERAL, &color_value, 1,
                       &image_subres_range);

  if ((result = vkEndCommandBuffer(vk_command_buffer_)) != VK_SUCCESS) {
    RTN_MSG(false, "vkEndCommandBuffer failed: %d\n", result);
  }

  image_initialized_ = true;

  return true;
}

bool VkReadbackTest::Exec(VkFence fence) {
  if (!Submit(fence)) {
    return false;
  }
  return Wait();
}

bool VkReadbackTest::Submit(VkFence fence) {
  VkSubmitInfo submit_info = {
      .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
      .pNext = nullptr,
      .waitSemaphoreCount = 0,
      .pWaitSemaphores = nullptr,
      .pWaitDstStageMask = nullptr,
      .commandBufferCount = 1,
      .pCommandBuffers = &vk_command_buffer_,
      .signalSemaphoreCount = 0,
      .pSignalSemaphores = nullptr,
  };

  VkResult result;
  if ((result = vkQueueSubmit(vk_queue_, 1, &submit_info, fence)) != VK_SUCCESS) {
    RTN_MSG(false, "vkQueueSubmit failed\n");
  }
  return true;
}

bool VkReadbackTest::Wait() {
  VkResult result = vkQueueWaitIdle(vk_queue_);
  if (result != VK_SUCCESS) {
    RTN_MSG(false, "vkQueueWaitIdle failed: %d\n", result);
  }
  return true;
}

bool VkReadbackTest::Readback() {
  VkResult result;
  void* addr;

  VkDeviceMemory vk_device_memory =
      ext_ == VkReadbackTest::NONE ? vk_device_memory_ : vk_imported_device_memory_;

  if ((result = vkMapMemory(vk_device_, vk_device_memory, 0, VK_WHOLE_SIZE, 0, &addr)) !=
      VK_SUCCESS) {
    RTN_MSG(false, "vkMapMemory failed: %d\n", result);
  }

  auto* data = reinterpret_cast<uint32_t*>(static_cast<uint8_t*>(addr) + bind_offset_);

  const uint32_t kExpectedValue = 0xBF8000FF;
  uint32_t mismatches = 0;
  for (uint32_t i = 0; i < kWidth * kHeight; i++) {
    if (data[i] != kExpectedValue) {
      constexpr int kMaxMismatches = 10;
      if (mismatches++ < kMaxMismatches) {
        fprintf(stderr, "Value Mismatch at index %d - expected 0x%04x, got 0x%08x\n", i,
                kExpectedValue, data[i]);
      }
    }
  }
  if (mismatches) {
    fprintf(stdout, "****** Test Failed! %d mismatches\n", mismatches);
  }

  vkUnmapMemory(vk_device_, vk_device_memory);

  return mismatches == 0;
}
