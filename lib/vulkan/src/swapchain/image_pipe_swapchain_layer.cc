// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <algorithm>
#include <stdio.h>
#include <vector>

#include "lib/images/fidl/image_pipe.fidl.h"
#include "lib/fidl/cpp/bindings/synchronous_interface_ptr.h"
#include "vk_dispatch_table_helper.h"
#include "vk_layer_config.h"
#include "vk_layer_extension_utils.h"
#include "vk_layer_logging.h"
#include "vk_layer_utils.h"
#include "vk_loader_platform.h"
#include "vulkan/vk_layer.h"
#include "vulkan/vulkan.h"
#include <unordered_map>

namespace image_pipe_swapchain {

// Zero is a invalid ID in the ImagePipe interface, so offset index by one
static inline uint32_t ImageIdFromIndex(uint32_t index) {
  return index + 1;
}

struct LayerData {
    VkInstance instance;
    VkLayerDispatchTable* device_dispatch_table;
    VkLayerInstanceDispatchTable* instance_dispatch_table;
};

// Global because thats how the layer code in the loader works and I dont know how to make it work
// otherwise
std::unordered_map<void*, LayerData*> layer_data_map;

constexpr VkExtensionProperties instance_extensions[] = {
    {VK_EXT_DEBUG_REPORT_EXTENSION_NAME, VK_EXT_DEBUG_REPORT_SPEC_VERSION}};

constexpr VkLayerProperties swapchain_layer = {
    "VK_LAYER_GOOGLE_image_pipe_swapchain", VK_LAYER_API_VERSION, 1, "Image Pipe Swapchain",
};

struct SupportedImageProperties {
    VkExtent2D size;
    std::vector<VkSurfaceFormatKHR> formats;
};

struct ImagePipeSurface {
    scenic::ImagePipeSyncPtr image_pipe;
    SupportedImageProperties supported_properties;
};

struct PendingImageInfo {
    zx::event release_fence;
    uint32_t image_index;
};

class ImagePipeSwapchain {
public:
    ImagePipeSwapchain(SupportedImageProperties supported_properties,
                       scenic::ImagePipeSyncPtr image_pipe)
        : supported_properties_(supported_properties), image_pipe_(std::move(image_pipe)),
          image_pipe_closed_(false)
    {
    }

    VkResult Initialize(VkDevice device, const VkSwapchainCreateInfoKHR* pCreateInfo,
                        const VkAllocationCallbacks* pAllocator);

    void Cleanup(VkDevice device, const VkAllocationCallbacks* pAllocator);

    VkResult GetSwapchainImages(uint32_t* pCount, VkImage* pSwapchainImages);
    VkResult AcquireNextImage(uint64_t timeout_ns, VkSemaphore semaphore, uint32_t* pImageIndex);
    VkResult Present(uint32_t index);

private:
    SupportedImageProperties supported_properties_;
    scenic::ImagePipeSyncPtr image_pipe_;
    std::vector<VkImage> images_;
    std::vector<VkDeviceMemory> memories_;
    std::vector<uint32_t> acquired_ids_;
    std::vector<uint32_t> available_ids_;
    std::vector<PendingImageInfo> pending_images_;
    bool image_pipe_closed_;
};

VkResult ImagePipeSwapchain::Initialize(VkDevice device,
                                        const VkSwapchainCreateInfoKHR* pCreateInfo,
                                        const VkAllocationCallbacks* pAllocator)
{
    VkResult result;
    VkLayerDispatchTable* pDisp =
        GetLayerDataPtr(get_dispatch_key(device), layer_data_map)->device_dispatch_table;

    bool scanout_tiling_enabled = false;
    uint32_t instance_extension_count;
    result = vkEnumerateInstanceExtensionProperties(nullptr, &instance_extension_count, nullptr);
    if (result != VK_SUCCESS)
        return result;

    if (instance_extension_count > 0) {
        std::vector<VkExtensionProperties> instance_extensions(instance_extension_count);
        result = vkEnumerateInstanceExtensionProperties(nullptr, &instance_extension_count,
                                                        instance_extensions.data());
        if (result != VK_SUCCESS)
            return result;

        for (uint32_t i = 0; i < instance_extension_count; i++) {
            if (!strcmp(VK_GOOGLE_IMAGE_TILING_SCANOUT_EXTENSION_NAME,
                        instance_extensions[i].extensionName)) {
                scanout_tiling_enabled = true;
                break;
            }
        }
    }

    // TODO(MA-345) support display tiling on request
    scanout_tiling_enabled = false;

    uint32_t num_images = pCreateInfo->minImageCount;
    assert(pCreateInfo->sType == VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR);
    for (uint32_t i = 0; i < num_images; i++) {
        // Allocate a buffer.
        VkImage image;
        uint32_t width = pCreateInfo->imageExtent.width;
        uint32_t height = pCreateInfo->imageExtent.height;
        VkImageCreateInfo create_info{
            .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
            .pNext = nullptr,
            .flags = VK_IMAGE_CREATE_MUTABLE_FORMAT_BIT,
            .imageType = VK_IMAGE_TYPE_2D,
            .format = pCreateInfo->imageFormat,
            .extent = {.width = width, .height = height, .depth = 1},
            .mipLevels = 1,
            .arrayLayers = 1,
            .samples = VK_SAMPLE_COUNT_1_BIT,
            .tiling =
                scanout_tiling_enabled ? VK_IMAGE_TILING_SCANOUT_GOOGLE : VK_IMAGE_TILING_OPTIMAL,
            .usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
            .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
            .queueFamilyIndexCount = 0,
            .pQueueFamilyIndices = nullptr,
            .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
        };

        result = pDisp->CreateImage(device, &create_info, pAllocator, &image);
        if (result != VK_SUCCESS) {
            FXL_DLOG(ERROR) << "VkCreateImage failed: " << result;
            return result;
        }
        images_.push_back(image);
        constexpr VkImageSubresource subres = {
            .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT, .mipLevel = 0, .arrayLayer = 0,
        };
        VkSubresourceLayout layout;
        pDisp->GetImageSubresourceLayout(device, image, &subres, &layout);

        VkMemoryRequirements memory_requirements;
        pDisp->GetImageMemoryRequirements(device, image, &memory_requirements);

        VkMemoryAllocateInfo alloc_info{
            .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
            .pNext = nullptr,
            .allocationSize = memory_requirements.size,
            .memoryTypeIndex = 0,
        };
        VkDeviceMemory device_mem;
        result = pDisp->AllocateMemory(device, &alloc_info, pAllocator, &device_mem);
        if (result != VK_SUCCESS) {
            FXL_DLOG(ERROR) << "vkAllocMemory failed: " << result;
            return result;
        }
        memories_.push_back(device_mem);
        result = pDisp->BindImageMemory(device, image, device_mem, 0);
        if (result != VK_SUCCESS) {
            FXL_DLOG(ERROR) << "vkBindImageMemory failed: " << result;
            return result;
        }
        uint32_t vmo_handle;
        result = pDisp->ExportDeviceMemoryMAGMA(device, device_mem, &vmo_handle);
        if (result != VK_SUCCESS) {
            FXL_DLOG(ERROR) << "vkExportDeviceMemoryMAGMA failed: " << result;
            return result;
        }

        zx::vmo vmo(vmo_handle);

        auto image_info = scenic::ImageInfo::New();
        image_info->width = width;
        image_info->height = height;
        image_info->stride = layout.rowPitch;
        image_info->pixel_format = scenic::ImageInfo::PixelFormat::BGRA_8;
        image_info->color_space = scenic::ImageInfo::ColorSpace::SRGB;
        image_info->tiling = scenic::ImageInfo::Tiling::GPU_OPTIMAL;

        image_pipe_->AddImage(ImageIdFromIndex(i), std::move(image_info),
                              std::move(vmo),
                              scenic::MemoryType::VK_DEVICE_MEMORY, 0);

        available_ids_.push_back(i);
    }
    return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL CreateSwapchainKHR(VkDevice device,
                                                  const VkSwapchainCreateInfoKHR* pCreateInfo,
                                                  const VkAllocationCallbacks* pAllocator,
                                                  VkSwapchainKHR* pSwapchain)
{
    VkResult ret = VK_ERROR_INITIALIZATION_FAILED;

    auto image_pipe_surface = reinterpret_cast<ImagePipeSurface*>(pCreateInfo->surface);
    SupportedImageProperties supported_properties = image_pipe_surface->supported_properties;

    auto swapchain = std::make_unique<ImagePipeSwapchain>(
        supported_properties, std::move(image_pipe_surface->image_pipe));

    ret = swapchain->Initialize(device, pCreateInfo, pAllocator);
    if (ret != VK_SUCCESS) {
        swapchain->Cleanup(device, pAllocator);
        FXL_DLOG(ERROR) << "failed to create swapchain: " << ret;
        return ret;
    }
    *pSwapchain = reinterpret_cast<VkSwapchainKHR>(swapchain.release());
    return VK_SUCCESS;
}

void ImagePipeSwapchain::Cleanup(VkDevice device, const VkAllocationCallbacks* pAllocator)
{
    VkLayerDispatchTable* pDisp =
        GetLayerDataPtr(get_dispatch_key(device), layer_data_map)->device_dispatch_table;
    for (auto image : images_)
        pDisp->DestroyImage(device, image, pAllocator);
    for (auto memory : memories_)
        pDisp->FreeMemory(device, memory, pAllocator);
}

VKAPI_ATTR void VKAPI_CALL DestroySwapchainKHR(VkDevice device, VkSwapchainKHR vk_swapchain,
                                               const VkAllocationCallbacks* pAllocator)
{
    auto swapchain = reinterpret_cast<ImagePipeSwapchain*>(vk_swapchain);
    swapchain->Cleanup(device, pAllocator);
    delete reinterpret_cast<ImagePipeSwapchain*>(swapchain);
}

VkResult ImagePipeSwapchain::GetSwapchainImages(uint32_t* pCount, VkImage* pSwapchainImages)
{
    if (image_pipe_closed_)
        return VK_ERROR_DEVICE_LOST;

    if (pSwapchainImages == NULL) {
        *pCount = images_.size();
        return VK_SUCCESS;
    }

    assert(images_.size() <= *pCount);

    for (uint32_t i = 0; i < images_.size(); i++)
        pSwapchainImages[i] = images_[i];

    *pCount = images_.size();
    return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL GetSwapchainImagesKHR(VkDevice device, VkSwapchainKHR vk_swapchain,
                                                     uint32_t* pCount, VkImage* pSwapchainImages)
{
    auto swapchain = reinterpret_cast<ImagePipeSwapchain*>(vk_swapchain);
    return swapchain->GetSwapchainImages(pCount, pSwapchainImages);
}

VkResult ImagePipeSwapchain::AcquireNextImage(uint64_t timeout_ns, VkSemaphore semaphore,
                                              uint32_t* pImageIndex)
{
    if (available_ids_.empty()) {
        // only way this can happen is if there are 0 images or if the client has already acquired
        // all images
        FXL_DCHECK(!pending_images_.empty());
        if (timeout_ns == 0)
            return VK_NOT_READY;
        // wait for image to become available
        zx_signals_t pending;

        zx_status_t status = pending_images_[0].release_fence.wait_one(
            ZX_EVENT_SIGNALED,
            timeout_ns == UINT64_MAX ? ZX_TIME_INFINITE : zx_deadline_after(timeout_ns), &pending);
        if (status == ZX_ERR_TIMED_OUT) {
            return VK_TIMEOUT;
        } else if (status != ZX_OK) {
            FXL_DLOG(ERROR) << "event::wait_one returned " << status;
            return VK_ERROR_DEVICE_LOST;
        }
        FXL_DCHECK(pending & ZX_EVENT_SIGNALED);

        available_ids_.push_back(pending_images_[0].image_index);
        pending_images_.erase(pending_images_.begin());
    }
    *pImageIndex = available_ids_[0];
    available_ids_.erase(available_ids_.begin());
    acquired_ids_.push_back(*pImageIndex);
    return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL AcquireNextImageKHR(VkDevice device, VkSwapchainKHR vk_swapchain,
                                                   uint64_t timeout, VkSemaphore semaphore,
                                                   VkFence fence, uint32_t* pImageIndex)
{
    // TODO(MA-264) handle this correctly
    FXL_CHECK(fence == VK_NULL_HANDLE);
    FXL_CHECK(semaphore == VK_NULL_HANDLE);

    auto swapchain = reinterpret_cast<ImagePipeSwapchain*>(vk_swapchain);
    return swapchain->AcquireNextImage(timeout, semaphore, pImageIndex);
}

VkResult ImagePipeSwapchain::Present(uint32_t index)
{
    if (image_pipe_closed_)
        return VK_ERROR_DEVICE_LOST;

    auto iter = std::find(acquired_ids_.begin(), acquired_ids_.end(), index);
    FXL_DCHECK(iter != acquired_ids_.end());
    acquired_ids_.erase(iter);

    zx::event acquire_fence, release_fence;
    zx_status_t status;
    status = zx::event::create(0, &acquire_fence);
    if (status != ZX_OK)
        return VK_ERROR_DEVICE_LOST;

    status = zx::event::create(0, &release_fence);
    if (status != ZX_OK)
        return VK_ERROR_DEVICE_LOST;

    status = acquire_fence.signal(0u, ZX_EVENT_SIGNALED);
    if (status) {
        FXL_DLOG(ERROR) << "failed to signal fence event, zx::event::signal() failed with status "
                        << status;
        return VK_ERROR_DEVICE_LOST;
    }

    zx::event image_release_fence;
    status = release_fence.duplicate(ZX_RIGHT_SAME_RIGHTS, &image_release_fence);
    if (status) {
        FXL_DLOG(ERROR)
            << "failed to duplicate release fence, zx::event::duplicate() failed with status "
            << status;
        return VK_ERROR_DEVICE_LOST;
    }

    pending_images_.push_back({std::move(image_release_fence), index});

    auto acquire_fences = fidl::Array<zx::event>::New(1);
    auto release_fences = fidl::Array<zx::event>::New(1);
    acquire_fences.push_back(std::move(acquire_fence));
    release_fences.push_back(std::move(release_fence));

    scenic::PresentationInfoPtr info;
    image_pipe_->PresentImage(ImageIdFromIndex(index), 0,
                              std::move(acquire_fences),
                              std::move(release_fences), &info);

    return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL QueuePresentKHR(VkQueue queue, const VkPresentInfoKHR* pPresentInfo)
{

    VkLayerDispatchTable* pDisp =
        GetLayerDataPtr(get_dispatch_key(queue), layer_data_map)->device_dispatch_table;

    // TODO(MA-264) Should export semaphores to an zx::event handle and give to ImagePipe consumer.
    // For now we just idle the queue
    pDisp->QueueWaitIdle(queue);

    for (uint32_t i = 0; i < pPresentInfo->swapchainCount; i++) {
        auto swapchain = reinterpret_cast<ImagePipeSwapchain*>(pPresentInfo->pSwapchains[i]);
        VkResult result = swapchain->Present(pPresentInfo->pImageIndices[i]);
        if (pPresentInfo->pResults) {
            pPresentInfo->pResults[i] = result;
        } else if (result != VK_SUCCESS) {
            return result;
        }
    }
    return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL GetPhysicalDeviceSurfaceSupportKHR(VkPhysicalDevice physicalDevice,
                                                                  uint32_t queueFamilyIndex,
                                                                  const VkSurfaceKHR surface,
                                                                  VkBool32* pSupported)
{
    *pSupported = surface != nullptr ? VK_TRUE : VK_FALSE;
    return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL CreateMagmaSurfaceKHR(VkInstance instance,
                                                     const VkMagmaSurfaceCreateInfoKHR* pCreateInfo,
                                                     const VkAllocationCallbacks* pAllocator,
                                                     VkSurfaceKHR* pSurface)
{
    auto surface = new ImagePipeSurface;
    std::vector<VkSurfaceFormatKHR> formats(
        {{VK_FORMAT_B8G8R8A8_UNORM, VK_COLORSPACE_SRGB_NONLINEAR_KHR}});
    surface->supported_properties = {{pCreateInfo->width, pCreateInfo->height}, formats};
    surface->image_pipe = scenic::ImagePipeSyncPtr::Create(
        fidl::InterfaceHandle<scenic::ImagePipe>(zx::channel(pCreateInfo->imagePipeHandle), 0u));
    *pSurface = reinterpret_cast<VkSurfaceKHR>(surface);
    return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL DestroySurfaceKHR(VkInstance instance, VkSurfaceKHR surface,
                                             const VkAllocationCallbacks* pAllocator)
{
    delete reinterpret_cast<ImagePipeSurface*>(surface);
}

VKAPI_ATTR VkBool32 VKAPI_CALL GetPhysicalDeviceMagmaPresentationSupportKHR(
    VkPhysicalDevice physicalDevice, uint32_t queueFamilyIndex)
{
    return VK_TRUE;
}

VKAPI_ATTR VkResult VKAPI_CALL
GetPhysicalDeviceSurfaceCapabilitiesKHR(VkPhysicalDevice physicalDevice, VkSurfaceKHR surface,
                                        VkSurfaceCapabilitiesKHR* pSurfaceCapabilities)
{

    auto image_pipe_surface = reinterpret_cast<ImagePipeSurface*>(surface);
    SupportedImageProperties supported_properties = image_pipe_surface->supported_properties;

    pSurfaceCapabilities->minImageCount = 2;
    pSurfaceCapabilities->maxImageCount = 0;
    pSurfaceCapabilities->currentExtent = supported_properties.size;
    pSurfaceCapabilities->minImageExtent = supported_properties.size;
    pSurfaceCapabilities->maxImageExtent = supported_properties.size;
    pSurfaceCapabilities->supportedTransforms = VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR;
    pSurfaceCapabilities->currentTransform = VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR;
    pSurfaceCapabilities->maxImageArrayLayers = 1;
    pSurfaceCapabilities->supportedUsageFlags =
        VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    pSurfaceCapabilities->supportedCompositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL
GetPhysicalDeviceSurfaceFormatsKHR(VkPhysicalDevice physicalDevice, const VkSurfaceKHR surface,
                                   uint32_t* pCount, VkSurfaceFormatKHR* pSurfaceFormats)
{
    auto image_pipe_surface = reinterpret_cast<ImagePipeSurface*>(surface);
    SupportedImageProperties supported_properties = image_pipe_surface->supported_properties;
    if (pSurfaceFormats == nullptr) {
        *pCount = supported_properties.formats.size();
        return VK_SUCCESS;
    }
    FXL_DCHECK(*pCount >= supported_properties.formats.size());
    memcpy(pSurfaceFormats, supported_properties.formats.data(),
           supported_properties.formats.size() * sizeof(VkSurfaceFormatKHR));
    *pCount = supported_properties.formats.size();
    return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL
GetPhysicalDeviceSurfacePresentModesKHR(VkPhysicalDevice physicalDevice, const VkSurfaceKHR surface,
                                        uint32_t* pCount, VkPresentModeKHR* pPresentModes)
{
    constexpr int present_mode_count = 1;
    constexpr VkPresentModeKHR present_modes[] = {VK_PRESENT_MODE_FIFO_KHR};
    if (pPresentModes == nullptr) {
        *pCount = present_mode_count;
        return VK_SUCCESS;
    }
    FXL_DCHECK(*pCount >= present_mode_count);
    memcpy(pPresentModes, present_modes, present_mode_count);
    *pCount = present_mode_count;
    return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL CreateInstance(const VkInstanceCreateInfo* pCreateInfo,
                                              const VkAllocationCallbacks* pAllocator,
                                              VkInstance* pInstance)
{

    VkLayerInstanceCreateInfo* chain_info = get_chain_info(pCreateInfo, VK_LAYER_LINK_INFO);

    assert(chain_info->u.pLayerInfo);
    PFN_vkGetInstanceProcAddr fpGetInstanceProcAddr =
        chain_info->u.pLayerInfo->pfnNextGetInstanceProcAddr;
    PFN_vkCreateInstance fpCreateInstance =
        (PFN_vkCreateInstance)fpGetInstanceProcAddr(NULL, "vkCreateInstance");
    if (fpCreateInstance == NULL) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    // Advance the link info for the next element on the chain
    chain_info->u.pLayerInfo = chain_info->u.pLayerInfo->pNext;

    VkResult result = fpCreateInstance(pCreateInfo, pAllocator, pInstance);
    if (result != VK_SUCCESS)
        return result;

    LayerData* my_data = GetLayerDataPtr(get_dispatch_key(*pInstance), layer_data_map);
    my_data->instance = *pInstance;
    my_data->instance_dispatch_table = new VkLayerInstanceDispatchTable;
    layer_init_instance_dispatch_table(*pInstance, my_data->instance_dispatch_table,
                                       fpGetInstanceProcAddr);

    return result;
}

VKAPI_ATTR void VKAPI_CALL DestroyInstance(VkInstance instance,
                                           const VkAllocationCallbacks* pAllocator)
{

    dispatch_key key = get_dispatch_key(instance);
    LayerData* my_data = GetLayerDataPtr(key, layer_data_map);

    delete my_data->instance_dispatch_table;
    layer_data_map.erase(key);
}

VKAPI_ATTR VkResult VKAPI_CALL CreateDevice(VkPhysicalDevice gpu,
                                            const VkDeviceCreateInfo* pCreateInfo,
                                            const VkAllocationCallbacks* pAllocator,
                                            VkDevice* pDevice)
{
    LayerData* my_instance_data = GetLayerDataPtr(get_dispatch_key(gpu), layer_data_map);
    VkLayerDeviceCreateInfo* chain_info = get_chain_info(pCreateInfo, VK_LAYER_LINK_INFO);

    assert(chain_info->u.pLayerInfo);
    PFN_vkGetInstanceProcAddr fpGetInstanceProcAddr =
        chain_info->u.pLayerInfo->pfnNextGetInstanceProcAddr;
    PFN_vkGetDeviceProcAddr fpGetDeviceProcAddr =
        chain_info->u.pLayerInfo->pfnNextGetDeviceProcAddr;
    PFN_vkCreateDevice fpCreateDevice =
        (PFN_vkCreateDevice)fpGetInstanceProcAddr(my_instance_data->instance, "vkCreateDevice");
    if (fpCreateDevice == NULL) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    // Advance the link info for the next element on the chain
    chain_info->u.pLayerInfo = chain_info->u.pLayerInfo->pNext;

    VkResult result = fpCreateDevice(gpu, pCreateInfo, pAllocator, pDevice);
    if (result != VK_SUCCESS) {
        return result;
    }

    LayerData* my_device_data = GetLayerDataPtr(get_dispatch_key(*pDevice), layer_data_map);

    // Setup device dispatch table
    my_device_data->device_dispatch_table = new VkLayerDispatchTable;
    layer_init_device_dispatch_table(*pDevice, my_device_data->device_dispatch_table,
                                     fpGetDeviceProcAddr);

    return result;
}

VKAPI_ATTR void VKAPI_CALL DestroyDevice(VkDevice device, const VkAllocationCallbacks* pAllocator)
{
    dispatch_key key = get_dispatch_key(device);
    LayerData* dev_data = GetLayerDataPtr(key, layer_data_map);
    dev_data->device_dispatch_table->DestroyDevice(device, pAllocator);
    layer_data_map.erase(key);
}

VKAPI_ATTR VkResult VKAPI_CALL EnumerateInstanceLayerProperties(uint32_t* pCount,
                                                                VkLayerProperties* pProperties)
{

    return util_GetLayerProperties(1, &swapchain_layer, pCount, pProperties);
}

VKAPI_ATTR VkResult VKAPI_CALL EnumerateDeviceLayerProperties(VkPhysicalDevice physicalDevice,
                                                              uint32_t* pCount,
                                                              VkLayerProperties* pProperties)
{

    return util_GetLayerProperties(1, &swapchain_layer, pCount, pProperties);
}

VKAPI_ATTR VkResult VKAPI_CALL EnumerateInstanceExtensionProperties(
    const char* pLayerName, uint32_t* pCount, VkExtensionProperties* pProperties)
{

    if (pLayerName && !strcmp(pLayerName, swapchain_layer.layerName))
        return util_GetExtensionProperties(1, instance_extensions, pCount, pProperties);

    return VK_ERROR_LAYER_NOT_PRESENT;
}

VKAPI_ATTR VkResult VKAPI_CALL
EnumerateDeviceExtensionProperties(VkPhysicalDevice physicalDevice, const char* pLayerName,
                                   uint32_t* pCount, VkExtensionProperties* pProperties)
{

    if (pLayerName && !strcmp(pLayerName, swapchain_layer.layerName))
        return util_GetExtensionProperties(0, nullptr, pCount, pProperties);

    assert(physicalDevice);

    dispatch_key key = get_dispatch_key(physicalDevice);
    LayerData* my_data = GetLayerDataPtr(key, layer_data_map);
    return my_data->instance_dispatch_table->EnumerateDeviceExtensionProperties(
        physicalDevice, NULL, pCount, pProperties);
}

VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL GetDeviceProcAddr(VkDevice device, const char* funcName);
VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL GetInstanceProcAddr(VkInstance instance,
                                                             const char* funcName);

static inline PFN_vkVoidFunction layer_intercept_proc(const char* name)
{
    if (!name || name[0] != 'v' || name[1] != 'k')
        return NULL;
    name += 2;
    if (!strcmp(name, "GetDeviceProcAddr"))
        return reinterpret_cast<PFN_vkVoidFunction>(GetDeviceProcAddr);
    if (!strcmp(name, "CreateInstance"))
        return reinterpret_cast<PFN_vkVoidFunction>(CreateInstance);
    if (!strcmp(name, "DestroyInstance"))
        return reinterpret_cast<PFN_vkVoidFunction>(DestroyInstance);
    if (!strcmp(name, "CreateDevice"))
        return reinterpret_cast<PFN_vkVoidFunction>(CreateDevice);
    if (!strcmp(name, "DestroyDevice"))
        return reinterpret_cast<PFN_vkVoidFunction>(DestroyDevice);
    if (!strcmp("CreateSwapchainKHR", name))
        return reinterpret_cast<PFN_vkVoidFunction>(CreateSwapchainKHR);
    if (!strcmp("DestroySwapchainKHR", name))
        return reinterpret_cast<PFN_vkVoidFunction>(DestroySwapchainKHR);
    if (!strcmp("GetSwapchainImagesKHR", name))
        return reinterpret_cast<PFN_vkVoidFunction>(GetSwapchainImagesKHR);
    if (!strcmp("AcquireNextImageKHR", name))
        return reinterpret_cast<PFN_vkVoidFunction>(AcquireNextImageKHR);
    if (!strcmp("QueuePresentKHR", name))
        return reinterpret_cast<PFN_vkVoidFunction>(QueuePresentKHR);
    if (!strcmp("EnumerateDeviceExtensionProperties", name))
        return reinterpret_cast<PFN_vkVoidFunction>(EnumerateDeviceExtensionProperties);
    if (!strcmp("EnumerateInstanceExtensionProperties", name))
        return reinterpret_cast<PFN_vkVoidFunction>(EnumerateInstanceExtensionProperties);
    if (!strcmp("EnumerateDeviceLayerProperties", name))
        return reinterpret_cast<PFN_vkVoidFunction>(EnumerateDeviceLayerProperties);
    if (!strcmp("EnumerateInstanceLayerProperties", name))
        return reinterpret_cast<PFN_vkVoidFunction>(EnumerateInstanceLayerProperties);
    return NULL;
}

static inline PFN_vkVoidFunction layer_intercept_instance_proc(const char* name)
{
    if (!name || name[0] != 'v' || name[1] != 'k')
        return NULL;
    name += 2;
    if (!strcmp(name, "GetInstanceProcAddr"))
        return reinterpret_cast<PFN_vkVoidFunction>(GetInstanceProcAddr);
    if (!strcmp(name, "CreateInstance"))
        return reinterpret_cast<PFN_vkVoidFunction>(CreateInstance);
    if (!strcmp(name, "DestroyInstance"))
        return reinterpret_cast<PFN_vkVoidFunction>(DestroyInstance);
    if (!strcmp("GetPhysicalDeviceSurfaceSupportKHR", name))
        return reinterpret_cast<PFN_vkVoidFunction>(GetPhysicalDeviceSurfaceSupportKHR);
    if (!strcmp("GetPhysicalDeviceSurfaceCapabilitiesKHR", name))
        return reinterpret_cast<PFN_vkVoidFunction>(GetPhysicalDeviceSurfaceCapabilitiesKHR);
    if (!strcmp("GetPhysicalDeviceSurfaceFormatsKHR", name))
        return reinterpret_cast<PFN_vkVoidFunction>(GetPhysicalDeviceSurfaceFormatsKHR);
    if (!strcmp("GetPhysicalDeviceSurfacePresentModesKHR", name))
        return reinterpret_cast<PFN_vkVoidFunction>(GetPhysicalDeviceSurfacePresentModesKHR);
    if (!strcmp("CreateMagmaSurfaceKHR", name))
        return reinterpret_cast<PFN_vkVoidFunction>(CreateMagmaSurfaceKHR);
    if (!strcmp("DestroySurfaceKHR", name))
        return reinterpret_cast<PFN_vkVoidFunction>(DestroySurfaceKHR);
    return NULL;
}

VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL GetDeviceProcAddr(VkDevice device, const char* funcName)
{
    PFN_vkVoidFunction addr;
    LayerData* dev_data;

    assert(device);

    addr = layer_intercept_proc(funcName);
    if (addr) {
        return addr;
    }

    dev_data = GetLayerDataPtr(get_dispatch_key(device), layer_data_map);

    VkLayerDispatchTable* pTable = dev_data->device_dispatch_table;

    if (pTable->GetDeviceProcAddr == NULL)
        return NULL;
    return pTable->GetDeviceProcAddr(device, funcName);
}

VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL GetInstanceProcAddr(VkInstance instance,
                                                             const char* funcName)
{

    PFN_vkVoidFunction addr;
    LayerData* my_data;

    addr = layer_intercept_instance_proc(funcName);
    if (!addr)
        addr = layer_intercept_proc(funcName);
    if (addr) {
        return addr;
    }

    if (!instance) {
        return nullptr;
    }

    my_data = GetLayerDataPtr(get_dispatch_key(instance), layer_data_map);

    VkLayerInstanceDispatchTable* pTable = my_data->instance_dispatch_table;
    if (pTable->GetInstanceProcAddr == NULL) {
        return NULL;
    }
    addr = pTable->GetInstanceProcAddr(instance, funcName);
    return addr;
}

VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL GetPhysicalDeviceProcAddr(VkInstance instance,
                                                                   const char* funcName)
{
    FXL_DCHECK(instance);

    LayerData* my_data;
    my_data = GetLayerDataPtr(get_dispatch_key(instance), layer_data_map);
    VkLayerInstanceDispatchTable* pTable = my_data->instance_dispatch_table;

    if (pTable->GetPhysicalDeviceProcAddr == NULL)
        return NULL;
    return pTable->GetPhysicalDeviceProcAddr(instance, funcName);
}

} // namespace image_pipe_swapchain

VK_LAYER_EXPORT VKAPI_ATTR VkResult VKAPI_CALL vkEnumerateInstanceExtensionProperties(
    const char* pLayerName, uint32_t* pCount, VkExtensionProperties* pProperties)
{
    return image_pipe_swapchain::EnumerateInstanceExtensionProperties(pLayerName, pCount,
                                                                      pProperties);
}

VK_LAYER_EXPORT VKAPI_ATTR VkResult VKAPI_CALL
vkEnumerateInstanceLayerProperties(uint32_t* pCount, VkLayerProperties* pProperties)
{
    return image_pipe_swapchain::EnumerateInstanceLayerProperties(pCount, pProperties);
}

VK_LAYER_EXPORT VKAPI_ATTR VkResult VKAPI_CALL vkEnumerateDeviceLayerProperties(
    VkPhysicalDevice physicalDevice, uint32_t* pCount, VkLayerProperties* pProperties)
{
    assert(physicalDevice == VK_NULL_HANDLE);
    return image_pipe_swapchain::EnumerateDeviceLayerProperties(VK_NULL_HANDLE, pCount,
                                                                pProperties);
}

VK_LAYER_EXPORT VKAPI_ATTR VkResult VKAPI_CALL
vkEnumerateDeviceExtensionProperties(VkPhysicalDevice physicalDevice, const char* pLayerName,
                                     uint32_t* pCount, VkExtensionProperties* pProperties)
{
    assert(physicalDevice == VK_NULL_HANDLE);
    return image_pipe_swapchain::EnumerateDeviceExtensionProperties(VK_NULL_HANDLE, pLayerName,
                                                                    pCount, pProperties);
}

VK_LAYER_EXPORT VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL vkGetDeviceProcAddr(VkDevice dev,
                                                                             const char* funcName)
{
    return image_pipe_swapchain::GetDeviceProcAddr(dev, funcName);
}

VK_LAYER_EXPORT VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL vkGetInstanceProcAddr(VkInstance instance,
                                                                               const char* funcName)
{
    return image_pipe_swapchain::GetInstanceProcAddr(instance, funcName);
}
