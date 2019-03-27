// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "gtest/gtest.h"

#define MAGMA_DLOG_ENABLE 1
#if defined(MAGMA_USE_SHIM)
#include "vulkan_shim.h"
#else
#include <vulkan/vulkan.h>
#endif
#include "fuchsia/sysmem/cpp/fidl.h"
#include <lib/fdio/directory.h>
#include <lib/zx/channel.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <vector>

#include "magma_util/dlog.h"
#include "magma_util/macros.h"

namespace {

class VulkanTest {
public:
    bool Initialize();
    bool Exec(VkFormat format, uint32_t width, bool linear);

private:
    bool InitVulkan();
    bool InitImage();

    bool is_initialized_ = false;
    VkPhysicalDevice vk_physical_device_;
    VkDevice vk_device_;
    VkQueue vk_queue_;
    VkImage vk_image_;
    VkDeviceMemory vk_device_memory_;
    VkCommandPool vk_command_pool_;
    VkCommandBuffer vk_command_buffer_;
    PFN_vkCreateBufferCollectionFUCHSIA vkCreateBufferCollectionFUCHSIA_;
    PFN_vkSetBufferCollectionConstraintsFUCHSIA vkSetBufferCollectionConstraintsFUCHSIA_;
    PFN_vkDestroyBufferCollectionFUCHSIA vkDestroyBufferCollectionFUCHSIA_;
};

bool VulkanTest::Initialize()
{
    if (is_initialized_)
        return false;

    if (!InitVulkan())
        return DRETF(false, "failed to initialize Vulkan");

    is_initialized_ = true;

    return true;
}

bool VulkanTest::InitVulkan()
{
    std::vector<const char*> enabled_extensions{};
    VkInstanceCreateInfo create_info{
        VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO, // VkStructureType             sType;
        nullptr,                                // const void*                 pNext;
        0,                                      // VkInstanceCreateFlags       flags;
        nullptr,                                // const VkApplicationInfo*    pApplicationInfo;
        0,                                      // uint32_t                    enabledLayerCount;
        nullptr,                                // const char* const*          ppEnabledLayerNames;
        static_cast<uint32_t>(enabled_extensions.size()),
        enabled_extensions.data(),
    };
    VkAllocationCallbacks* allocation_callbacks = nullptr;
    VkInstance instance;
    VkResult result;

    if ((result = vkCreateInstance(&create_info, allocation_callbacks, &instance)) != VK_SUCCESS)
        return DRETF(false, "vkCreateInstance failed %d", result);

    DLOG("vkCreateInstance succeeded");

    uint32_t physical_device_count;
    if ((result = vkEnumeratePhysicalDevices(instance, &physical_device_count, nullptr)) !=
        VK_SUCCESS)
        return DRETF(false, "vkEnumeratePhysicalDevices failed %d", result);

    if (physical_device_count < 1)
        return DRETF(false, "unexpected physical_device_count %d", physical_device_count);

    DLOG("vkEnumeratePhysicalDevices returned count %d", physical_device_count);

    std::vector<VkPhysicalDevice> physical_devices(physical_device_count);
    if ((result = vkEnumeratePhysicalDevices(instance, &physical_device_count,
                                             physical_devices.data())) != VK_SUCCESS)
        return DRETF(false, "vkEnumeratePhysicalDevices failed %d", result);

    for (auto device : physical_devices) {
        VkPhysicalDeviceProperties properties;
        vkGetPhysicalDeviceProperties(device, &properties);
        DLOG("PHYSICAL DEVICE: %s", properties.deviceName);
        DLOG("apiVersion 0x%x", properties.apiVersion);
        DLOG("driverVersion 0x%x", properties.driverVersion);
        DLOG("vendorID 0x%x", properties.vendorID);
        DLOG("deviceID 0x%x", properties.deviceID);
        DLOG("deviceType 0x%x", properties.deviceType);
    }

    uint32_t queue_family_count;
    vkGetPhysicalDeviceQueueFamilyProperties(physical_devices[0], &queue_family_count, nullptr);

    if (queue_family_count < 1)
        return DRETF(false, "invalid queue_family_count %d", queue_family_count);

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

    if (queue_family_index < 0)
        return DRETF(false, "couldn't find an appropriate queue");

    float queue_priorities[1] = {0.0};

    VkDeviceQueueCreateInfo queue_create_info = {.sType =
                                                     VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
                                                 .pNext = nullptr,
                                                 .flags = 0,
                                                 .queueFamilyIndex = 0,
                                                 .queueCount = 1,
                                                 .pQueuePriorities = queue_priorities};
    std::vector<const char*> enabled_device_extensions{VK_FUCHSIA_BUFFER_COLLECTION_EXTENSION_NAME};
    VkDeviceCreateInfo createInfo = {.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
                                     .pNext = nullptr,
                                     .flags = 0,
                                     .queueCreateInfoCount = 1,
                                     .pQueueCreateInfos = &queue_create_info,
                                     .enabledLayerCount = 0,
                                     .ppEnabledLayerNames = nullptr,
                                     .enabledExtensionCount =
                                         static_cast<uint32_t>(enabled_device_extensions.size()),
                                     .ppEnabledExtensionNames = enabled_device_extensions.data(),
                                     .pEnabledFeatures = nullptr};
    VkDevice vkdevice;

    if ((result = vkCreateDevice(physical_devices[0], &createInfo,
                                 nullptr /* allocationcallbacks */, &vkdevice)) != VK_SUCCESS)
        return DRETF(false, "vkCreateDevice failed: %d", result);

    vk_physical_device_ = physical_devices[0];
    vk_device_ = vkdevice;

    vkGetDeviceQueue(vkdevice, queue_family_index, 0, &vk_queue_);

    vkCreateBufferCollectionFUCHSIA_ = reinterpret_cast<PFN_vkCreateBufferCollectionFUCHSIA>(
        vkGetDeviceProcAddr(vk_device_, "vkCreateBufferCollectionFUCHSIA"));
    if (!vkCreateBufferCollectionFUCHSIA_) {
        return DRETF(false, "No vkCreateBufferCollectionFUCHSIA");
    }

    vkDestroyBufferCollectionFUCHSIA_ = reinterpret_cast<PFN_vkDestroyBufferCollectionFUCHSIA>(
        vkGetDeviceProcAddr(vk_device_, "vkDestroyBufferCollectionFUCHSIA"));
    if (!vkDestroyBufferCollectionFUCHSIA_) {
        return DRETF(false, "No vkDestroyBufferCollectionFUCHSIA");
    }

    vkSetBufferCollectionConstraintsFUCHSIA_ =
        reinterpret_cast<PFN_vkSetBufferCollectionConstraintsFUCHSIA>(
            vkGetDeviceProcAddr(vk_device_, "vkSetBufferCollectionConstraintsFUCHSIA"));
    if (!vkSetBufferCollectionConstraintsFUCHSIA_) {
        return DRETF(false, "No vkSetBufferCollectionConstraintsFUCHSIA");
    }

    return true;
}

bool VulkanTest::Exec(VkFormat format, uint32_t width, bool linear)
{
    VkResult result;
    fuchsia::sysmem::AllocatorSyncPtr sysmem_allocator;
    zx_status_t status = fdio_service_connect(
        "/svc/fuchsia.sysmem.Allocator", sysmem_allocator.NewRequest().TakeChannel().release());
    if (status != ZX_OK) {
        return DRETF(false, "fdio_service_connect failed: %d", status);
    }
    fuchsia::sysmem::BufferCollectionTokenSyncPtr vulkan_token;
    status = sysmem_allocator->AllocateSharedCollection(vulkan_token.NewRequest());
    if (status != ZX_OK) {
        return DRETF(false, "AllocateSharedCollection failed: %d", status);
    }
    fuchsia::sysmem::BufferCollectionTokenSyncPtr local_token;

    status =
        vulkan_token->Duplicate(std::numeric_limits<uint32_t>::max(), local_token.NewRequest());
    if (status != ZX_OK) {
        return DRETF(false, "Duplicate failed: %d", status);
    }
    status = local_token->Sync();
    if (status != ZX_OK) {
        return DRETF(false, "Sync failed: %d", status);
    }

    VkImageCreateInfo image_create_info = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
        .pNext = nullptr,
        .flags = 0u,
        .imageType = VK_IMAGE_TYPE_2D,
        .format = format,
        .extent = VkExtent3D{width, 64, 1},
        .mipLevels = 1,
        .arrayLayers = 1,
        .samples = VK_SAMPLE_COUNT_1_BIT,
        .tiling = linear ? VK_IMAGE_TILING_LINEAR : VK_IMAGE_TILING_OPTIMAL,
        // Only use sampled, because on Mali some other usages (like color attachment) aren't
        // supported for NV12, and some others (implementation-dependent) aren't supported with
        // AFBC.
        .usage = VK_IMAGE_USAGE_SAMPLED_BIT,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
        .queueFamilyIndexCount = 0,     // not used since not sharing
        .pQueueFamilyIndices = nullptr, // not used since not sharing
        .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
    };

    VkBufferCollectionCreateInfoFUCHSIA import_info = {
        .sType = VK_STRUCTURE_TYPE_BUFFER_COLLECTION_CREATE_INFO_FUCHSIA,
        .pNext = nullptr,
        .collectionToken = vulkan_token.Unbind().TakeChannel().release(),
    };
    VkBufferCollectionFUCHSIA collection;
    result = vkCreateBufferCollectionFUCHSIA_(vk_device_, &import_info, nullptr, &collection);
    if (result != VK_SUCCESS) {
        fprintf(stderr, "Failed to import buffer collection: %d\n", result);
        return false;
    }

    result = vkSetBufferCollectionConstraintsFUCHSIA_(vk_device_, collection, &image_create_info);

    if (result != VK_SUCCESS) {
        fprintf(stderr, "Failed to set buffer constraints: %d\n", result);
        return false;
    }

    fuchsia::sysmem::BufferCollectionSyncPtr sysmem_collection;
    status = sysmem_allocator->BindSharedCollection(std::move(local_token), sysmem_collection.NewRequest());
    if (status != ZX_OK) {
        return DRETF(false, "BindSharedCollection failed: %d", status);
    }
    fuchsia::sysmem::BufferCollectionConstraints constraints{};
    constraints.usage.vulkan = fuchsia::sysmem::vulkanUsageTransferDst;
    status = sysmem_collection->SetConstraints(true, constraints);
    if (status != ZX_OK) {
        return DRETF(false, "SetConstraints failed: %d", status);
    }

    zx_status_t allocation_status;
    fuchsia::sysmem::BufferCollectionInfo_2 buffer_collection_info{};
    status =
        sysmem_collection->WaitForBuffersAllocated(&allocation_status, &buffer_collection_info);
    if (status != ZX_OK || allocation_status != ZX_OK) {
        return DRETF(false, "WaitForBuffersAllocated failed: %d %d", status, allocation_status);
    }
    status = sysmem_collection->Close();
    if (status != ZX_OK) {
        return DRETF(false, "Close failed: %d", status);
    }

    fuchsia::sysmem::PixelFormat pixel_format =
        buffer_collection_info.settings.image_format_constraints.pixel_format;
    DLOG("Allocated format %d has_modifier %d modifier %lx\n", pixel_format.type,
         pixel_format.has_format_modifier, pixel_format.format_modifier.value);

    fidl::Encoder encoder(fidl::Encoder::NO_HEADER);
    encoder.Alloc(fidl::CodingTraits<fuchsia::sysmem::SingleBufferSettings>::encoded_size);
    buffer_collection_info.settings.Encode(&encoder, 0);
    std::vector<uint8_t> encoded_data = encoder.TakeBytes();

    vkDestroyBufferCollectionFUCHSIA_(vk_device_, collection, nullptr);

    VkFuchsiaImageFormatFUCHSIA image_format_fuchsia = {
        .sType = VK_STRUCTURE_TYPE_FUCHSIA_IMAGE_FORMAT_FUCHSIA,
        .pNext = nullptr,
        .imageFormat = encoded_data.data(),
        .imageFormatSize = static_cast<uint32_t>(encoded_data.size())};
    image_create_info.pNext = &image_format_fuchsia;

    result = vkCreateImage(vk_device_, &image_create_info, nullptr, &vk_image_);
    if (result != VK_SUCCESS)
        return DRETF(false, "vkCreateImage failed: %d", result);

    DLOG("image created");

    if (linear) {
        VkImageSubresource subresource = {.aspectMask =
                                              format == VK_FORMAT_G8_B8R8_2PLANE_420_UNORM_KHR
                                                  ? VK_IMAGE_ASPECT_PLANE_0_BIT
                                                  : VK_IMAGE_ASPECT_COLOR_BIT,
                                          .mipLevel = 0,
                                          .arrayLayer = 0};
        VkSubresourceLayout layout;
        vkGetImageSubresourceLayout(vk_device_, vk_image_, &subresource, &layout);

        VkDeviceSize min_bytes_per_pixel = format == VK_FORMAT_G8_B8R8_2PLANE_420_UNORM_KHR ? 1 : 4;
        EXPECT_LE(min_bytes_per_pixel * width, layout.rowPitch);
        EXPECT_LE(min_bytes_per_pixel * width * 64, layout.size);
    }

    VkMemoryRequirements memory_reqs;
    vkGetImageMemoryRequirements(vk_device_, vk_image_, &memory_reqs);
    // Use first supported type
    uint32_t memory_type = __builtin_ctz(memory_reqs.memoryTypeBits);

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

    VkDeviceMemory memory;
    if ((result = vkAllocateMemory(vk_device_, &alloc_info, nullptr, &memory)) != VK_SUCCESS) {
        return DRETF(false, "vkAllocateMemory failed");
    }

    result = vkBindImageMemory(vk_device_, vk_image_, memory, 0);
    if (result != VK_SUCCESS) {
        return DRETF(false, "vkBindImageMemory failed");
    }

    vkDestroyImage(vk_device_, vk_image_, nullptr);

    vkFreeMemory(vk_device_, memory, nullptr);

    DLOG("image destroyed");

    return true;
}

// Parameter is true if the image should be linear.
class VulkanExtensionTest : public ::testing::TestWithParam<bool> {
};

TEST_P(VulkanExtensionTest, BufferCollectionNV12)
{
    VulkanTest test;
    ASSERT_TRUE(test.Initialize());
    ASSERT_TRUE(test.Exec(VK_FORMAT_G8_B8R8_2PLANE_420_UNORM, 64, GetParam()));
}

TEST_P(VulkanExtensionTest, BufferCollectionNV12_1025)
{
    VulkanTest test;
    ASSERT_TRUE(test.Initialize());
    ASSERT_TRUE(test.Exec(VK_FORMAT_G8_B8R8_2PLANE_420_UNORM, 1025, GetParam()));
}

TEST_P(VulkanExtensionTest, BufferCollectionRGBA)
{
    VulkanTest test;
    ASSERT_TRUE(test.Initialize());
    ASSERT_TRUE(test.Exec(VK_FORMAT_R8G8B8A8_UNORM, 64, GetParam()));
}

TEST_P(VulkanExtensionTest, BufferCollectionRGBA_1025)
{
    VulkanTest test;
    ASSERT_TRUE(test.Initialize());
    ASSERT_TRUE(test.Exec(VK_FORMAT_R8G8B8A8_UNORM, 1025, GetParam()));
}

INSTANTIATE_TEST_SUITE_P(, VulkanExtensionTest, ::testing::Bool());

} // namespace
