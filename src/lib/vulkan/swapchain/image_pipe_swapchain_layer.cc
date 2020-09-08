// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#define FUCHSIA_LAYER 1

#if USE_IMAGEPIPE_SURFACE_FB
#include "image_pipe_surface_display.h"  // nogncheck
#else
#include "image_pipe_surface_async.h"  // nogncheck
#endif

#include <lib/trace/event.h>
#include <vk_dispatch_table_helper.h>
#include <vk_layer_data.h>
#include <vk_layer_extension_utils.h>
#include <vk_layer_utils_minimal.h>

#include <thread>
#include <unordered_map>
#include <vector>

#include <vulkan/vk_layer.h>

#define VK_LAYER_API_VERSION VK_MAKE_VERSION(1, 1, VK_HEADER_VERSION)

namespace image_pipe_swapchain {

// Useful for testing app performance without external restriction
// (due to composition, vsync, etc.)
#if SKIP_PRESENT
constexpr bool kSkipPresent = true;
#else
constexpr bool kSkipPresent = false;
#endif

struct LayerData {
  VkInstance instance;
  std::unique_ptr<VkLayerDispatchTable> device_dispatch_table;
  std::unique_ptr<VkLayerInstanceDispatchTable> instance_dispatch_table;
  std::unordered_map<VkDebugUtilsMessengerEXT, VkDebugUtilsMessengerCreateInfoEXT> debug_callbacks;
};

// Global because thats how the layer code in the loader works and I dont know
// how to make it work otherwise
std::unordered_map<void*, LayerData*> layer_data_map;

static const VkExtensionProperties instance_extensions[] = {
    {
        .extensionName = VK_KHR_SURFACE_EXTENSION_NAME,
        .specVersion = 25,
    },
    {
        .extensionName = VK_FUCHSIA_IMAGEPIPE_SURFACE_EXTENSION_NAME,
        .specVersion = 1,
    }};

static const VkExtensionProperties device_extensions[] = {{
    .extensionName = VK_KHR_SWAPCHAIN_EXTENSION_NAME,
    .specVersion = 68,
}};

constexpr VkLayerProperties swapchain_layer = {
#ifdef USE_IMAGEPIPE_LAYER_NAME
    USE_IMAGEPIPE_LAYER_NAME,
#else
    "VK_LAYER_FUCHSIA_imagepipe_swapchain",
#endif
    VK_LAYER_API_VERSION,
    1,
    "Image Pipe Swapchain",
};

struct ImagePipeImage {
  VkImage image;
  uint32_t id;
};

struct PendingImageInfo {
  zx::event release_fence;
  uint32_t image_index;
};

class ImagePipeSwapchain {
 public:
  ImagePipeSwapchain(ImagePipeSurface* surface)
      : surface_(surface), is_protected_(false), device_(VK_NULL_HANDLE) {}

  VkResult Initialize(VkDevice device, const VkSwapchainCreateInfoKHR* pCreateInfo,
                      const VkAllocationCallbacks* pAllocator);

  void Cleanup(VkDevice device, const VkAllocationCallbacks* pAllocator);

  VkResult GetSwapchainImages(uint32_t* pCount, VkImage* pSwapchainImages);
  VkResult AcquireNextImage(uint64_t timeout_ns, VkSemaphore semaphore, uint32_t* pImageIndex);
  VkResult Present(VkQueue queue, uint32_t index, uint32_t waitSemaphoreCount,
                   const VkSemaphore* pWaitSemaphores);

  void DebugMessage(VkDebugUtilsMessageSeverityFlagBitsEXT severity, const char* message);

 private:
  ImagePipeSurface* surface() { return surface_; }

  ImagePipeSurface* surface_;
  std::vector<ImagePipeImage> images_;
  std::vector<VkDeviceMemory> memories_;
  std::vector<VkSemaphore> semaphores_;
  std::vector<uint32_t> acquired_ids_;
  std::vector<PendingImageInfo> pending_images_;
  bool is_protected_;
  VkDevice device_;
};

///////////////////////////////////////////////////////////////////////////////

void ImagePipeSwapchain::DebugMessage(VkDebugUtilsMessageSeverityFlagBitsEXT severity,
                                      const char* message) {
  LayerData* device_data = GetLayerDataPtr(get_dispatch_key(device_), layer_data_map);
  LayerData* instance_data =
      GetLayerDataPtr(get_dispatch_key(device_data->instance), layer_data_map);

  VkDebugUtilsMessengerCallbackDataEXT callback_data = {};
  callback_data.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CALLBACK_DATA_EXT;
  callback_data.pMessage = message;

  for (auto& callback : instance_data->debug_callbacks) {
    if (!(severity & callback.second.messageSeverity))
      continue;
    if (!(VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT & callback.second.messageType))
      continue;
    callback.second.pfnUserCallback(severity, VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT,
                                    &callback_data, callback.second.pUserData);
  }
}

VkResult ImagePipeSwapchain::Initialize(VkDevice device,
                                        const VkSwapchainCreateInfoKHR* pCreateInfo,
                                        const VkAllocationCallbacks* pAllocator) {
  is_protected_ = pCreateInfo->flags & VK_SWAPCHAIN_CREATE_PROTECTED_BIT_KHR;
  VkResult result;
  VkLayerDispatchTable* pDisp =
      GetLayerDataPtr(get_dispatch_key(device), layer_data_map)->device_dispatch_table.get();
  uint32_t num_images = pCreateInfo->minImageCount;
  VkFlags usage = pCreateInfo->imageUsage & surface_->SupportedUsage();
  assert(pCreateInfo->sType == VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR);
  std::vector<ImagePipeSurface::ImageInfo> image_infos;

  if (!surface_->CreateImage(device, pDisp, pCreateInfo->imageFormat, usage, pCreateInfo->flags,
                             pCreateInfo->imageExtent, num_images, pAllocator, &image_infos)) {
    return VK_ERROR_OUT_OF_DEVICE_MEMORY;
  }
  for (uint32_t i = 0; i < num_images; i++) {
    images_.push_back({image_infos[i].image, image_infos[i].image_id});
    memories_.push_back(image_infos[i].memory);
    VkSemaphoreCreateInfo create_semaphore_info{
        .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO, .pNext = nullptr, .flags = 0};
    VkSemaphore semaphore;
    result = pDisp->CreateSemaphore(device, &create_semaphore_info, pAllocator, &semaphore);
    if (result != VK_SUCCESS) {
      fprintf(stderr, "vkCreateSemaphore failed: %d", result);
      return result;
    }
    semaphores_.push_back(semaphore);

    zx::event release_fence;
    zx_status_t status = zx::event::create(0, &release_fence);
    if (status != ZX_OK) {
      fprintf(stderr, "zx::event::create failed: %d\n", status);
      return VK_ERROR_DEVICE_LOST;
    }

    release_fence.signal(0, ZX_EVENT_SIGNALED);
    pending_images_.push_back({std::move(release_fence), i});
  }

  device_ = device;
  return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL CreateSwapchainKHR(VkDevice device,
                                                  const VkSwapchainCreateInfoKHR* pCreateInfo,
                                                  const VkAllocationCallbacks* pAllocator,
                                                  VkSwapchainKHR* pSwapchain) {
  VkResult ret = VK_ERROR_INITIALIZATION_FAILED;

  auto image_pipe_surface = reinterpret_cast<ImagePipeSurface*>(pCreateInfo->surface);
  auto swapchain = std::make_unique<ImagePipeSwapchain>(image_pipe_surface);

  ret = swapchain->Initialize(device, pCreateInfo, pAllocator);
  if (ret != VK_SUCCESS) {
    swapchain->Cleanup(device, pAllocator);
    fprintf(stderr, "failed to create swapchain: %d", ret);
    return ret;
  }
  *pSwapchain = reinterpret_cast<VkSwapchainKHR>(swapchain.release());
  return VK_SUCCESS;
}

void ImagePipeSwapchain::Cleanup(VkDevice device, const VkAllocationCallbacks* pAllocator) {
  VkLayerDispatchTable* pDisp =
      GetLayerDataPtr(get_dispatch_key(device), layer_data_map)->device_dispatch_table.get();

  for (auto& image : images_) {
    surface()->RemoveImage(image.id);
    pDisp->DestroyImage(device, image.image, pAllocator);
  }
  for (auto memory : memories_) {
    pDisp->FreeMemory(device, memory, pAllocator);
  }
  for (auto semaphore : semaphores_) {
    pDisp->DestroySemaphore(device, semaphore, pAllocator);
  }
}

VKAPI_ATTR void VKAPI_CALL DestroySwapchainKHR(VkDevice device, VkSwapchainKHR vk_swapchain,
                                               const VkAllocationCallbacks* pAllocator) {
  auto swapchain = reinterpret_cast<ImagePipeSwapchain*>(vk_swapchain);
  swapchain->Cleanup(device, pAllocator);
  delete reinterpret_cast<ImagePipeSwapchain*>(swapchain);
}

VkResult ImagePipeSwapchain::GetSwapchainImages(uint32_t* pCount, VkImage* pSwapchainImages) {
  if (pSwapchainImages == NULL) {
    *pCount = images_.size();
    return VK_SUCCESS;
  }

  assert(images_.size() <= *pCount);

  for (uint32_t i = 0; i < images_.size(); i++)
    pSwapchainImages[i] = images_[i].image;

  *pCount = images_.size();
  return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL GetSwapchainImagesKHR(VkDevice device, VkSwapchainKHR vk_swapchain,
                                                     uint32_t* pCount, VkImage* pSwapchainImages) {
  auto swapchain = reinterpret_cast<ImagePipeSwapchain*>(vk_swapchain);
  return swapchain->GetSwapchainImages(pCount, pSwapchainImages);
}

static void CrashDueToOutOfImages() { abort(); }

VkResult ImagePipeSwapchain::AcquireNextImage(uint64_t timeout_ns, VkSemaphore semaphore,
                                              uint32_t* pImageIndex) {
  if (surface_->IsLost())
    return VK_ERROR_SURFACE_LOST_KHR;
  if (pending_images_.empty()) {
    // All images acquired and none presented.  We will never acquire anything.
    if (timeout_ns == 0)
      return VK_NOT_READY;
    if (timeout_ns == UINT64_MAX) {
      // This goes against the VU, so we can crash to help detect bugs:
      //
      // If the number of currently acquired images is greater than the difference between the
      // number of images in swapchain and the value of VkSurfaceCapabilitiesKHR::minImageCount as
      // returned by a call to vkGetPhysicalDeviceSurfaceCapabilities2KHR with the surface used to
      // create swapchain, timeout must not be UINT64_MAX
      DebugMessage(VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT,
                   "Currently all images are pending. Crashing program.");
      CrashDueToOutOfImages();
    }

    std::this_thread::sleep_for(std::chrono::nanoseconds(timeout_ns));
    return VK_TIMEOUT;
  }

  bool wait_for_release_fence = false;

  if (semaphore == VK_NULL_HANDLE) {
    wait_for_release_fence = true;
  } else {
    zx_handle_t handle;

    if (surface()->CanPresentPendingImage()) {
      handle = pending_images_[0].release_fence.release();
    } else {
      zx::event signaled_event;
      zx_status_t status = zx::event::create(0, &signaled_event);
      if (status != ZX_OK) {
        fprintf(stderr, "event::create failed");
        return VK_SUCCESS;
      }
      signaled_event.signal(0, ZX_EVENT_SIGNALED);
      handle = signaled_event.release();
      wait_for_release_fence = true;
    }

    VkImportSemaphoreZirconHandleInfoFUCHSIA import_info = {
        .sType = VK_STRUCTURE_TYPE_TEMP_IMPORT_SEMAPHORE_ZIRCON_HANDLE_INFO_FUCHSIA,
        .pNext = nullptr,
        .semaphore = semaphore,
        .flags = VK_SEMAPHORE_IMPORT_TEMPORARY_BIT_KHR,
        .handleType = VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_TEMP_ZIRCON_EVENT_BIT_FUCHSIA,
        .handle = handle};

    VkLayerDispatchTable* pDisp =
        GetLayerDataPtr(get_dispatch_key(device_), layer_data_map)->device_dispatch_table.get();
    VkResult result = pDisp->ImportSemaphoreZirconHandleFUCHSIA(device_, &import_info);
    if (result != VK_SUCCESS) {
      fprintf(stderr, "semaphore import failed: %d", result);
      return VK_SUCCESS;
    }
  }

  if (wait_for_release_fence) {
    // Wait for image to become available.
    zx_signals_t pending;
    zx_status_t status = pending_images_[0].release_fence.wait_one(
        ZX_EVENT_SIGNALED,
        timeout_ns == UINT64_MAX ? zx::time::infinite() : zx::deadline_after(zx::nsec(timeout_ns)),
        &pending);
    if (surface_->IsLost())
      return VK_ERROR_SURFACE_LOST_KHR;

    if (status == ZX_ERR_TIMED_OUT)
      return timeout_ns == 0ul ? VK_NOT_READY : VK_TIMEOUT;
    if (status != ZX_OK) {
      fprintf(stderr, "event::wait_one returned %d", status);
      return VK_ERROR_DEVICE_LOST;
    }
    assert(pending & ZX_EVENT_SIGNALED);
  }

  *pImageIndex = pending_images_[0].image_index;
  pending_images_.erase(pending_images_.begin());
  acquired_ids_.push_back(*pImageIndex);

  return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL AcquireNextImageKHR(VkDevice device, VkSwapchainKHR vk_swapchain,
                                                   uint64_t timeout, VkSemaphore semaphore,
                                                   VkFence fence, uint32_t* pImageIndex) {
  auto swapchain = reinterpret_cast<ImagePipeSwapchain*>(vk_swapchain);
  if (fence) {
    // TODO(MA-264) handle this correctly
    swapchain->DebugMessage(VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT,
                            "Image pipe swapchain doesn't support fences.");
    return VK_ERROR_DEVICE_LOST;
  }
  return swapchain->AcquireNextImage(timeout, semaphore, pImageIndex);
}

VkResult ImagePipeSwapchain::Present(VkQueue queue, uint32_t index, uint32_t waitSemaphoreCount,
                                     const VkSemaphore* pWaitSemaphores) {
  if (surface_->IsLost())
    return VK_ERROR_SURFACE_LOST_KHR;

  VkLayerDispatchTable* pDisp =
      GetLayerDataPtr(get_dispatch_key(queue), layer_data_map)->device_dispatch_table.get();

  zx::event acquire_fence;
  zx_status_t status = zx::event::create(0, &acquire_fence);
  if (status != ZX_OK) {
    fprintf(stderr, "zx::event::create failed: %d\n", status);
    return VK_ERROR_DEVICE_LOST;
  }

  zx::event image_acquire_fence;
  status = acquire_fence.duplicate(ZX_RIGHT_SAME_RIGHTS, &image_acquire_fence);
  if (status != ZX_OK) {
    fprintf(stderr,
            "failed to duplicate acquire fence, "
            "zx::event::duplicate() failed with status %d",
            status);
    return VK_ERROR_DEVICE_LOST;
  }

  VkImportSemaphoreZirconHandleInfoFUCHSIA import_info = {
      .sType = VK_STRUCTURE_TYPE_TEMP_IMPORT_SEMAPHORE_ZIRCON_HANDLE_INFO_FUCHSIA,
      .pNext = nullptr,
      .semaphore = semaphores_[index],
      .flags = VK_SEMAPHORE_IMPORT_TEMPORARY_BIT_KHR,
      .handleType = VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_TEMP_ZIRCON_EVENT_BIT_FUCHSIA,
      .handle = image_acquire_fence.release()};

  VkResult result = pDisp->ImportSemaphoreZirconHandleFUCHSIA(device_, &import_info);
  if (result != VK_SUCCESS) {
    fprintf(stderr, "semaphore import failed: %d", result);
    return VK_ERROR_SURFACE_LOST_KHR;
  }

  std::vector<VkPipelineStageFlags> flag_bits(waitSemaphoreCount,
                                              VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT);
  VkProtectedSubmitInfo protected_submit_info = {
      .sType = VK_STRUCTURE_TYPE_PROTECTED_SUBMIT_INFO,
      .pNext = nullptr,
      .protectedSubmit = VK_TRUE,
  };
  VkSubmitInfo submit_info = {.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
                              .pNext = is_protected_ ? &protected_submit_info : nullptr,
                              .waitSemaphoreCount = waitSemaphoreCount,
                              .pWaitSemaphores = pWaitSemaphores,
                              .pWaitDstStageMask = flag_bits.data(),
                              .signalSemaphoreCount = 1,
                              .pSignalSemaphores = &semaphores_[index]};
  result = pDisp->QueueSubmit(queue, 1, &submit_info, VK_NULL_HANDLE);
  if (result != VK_SUCCESS) {
    fprintf(stderr, "vkQueueSubmit failed with result %d", result);
    return VK_ERROR_SURFACE_LOST_KHR;
  }

  auto iter = std::find(acquired_ids_.begin(), acquired_ids_.end(), index);
  assert(iter != acquired_ids_.end());
  acquired_ids_.erase(iter);

  if (kSkipPresent) {
    pending_images_.push_back({std::move(acquire_fence), index});
  } else {
    zx::event release_fence;

    zx_status_t status = zx::event::create(0, &release_fence);
    if (status != ZX_OK) {
      fprintf(stderr, "zx::event::create failed: %d\n", status);
      return VK_ERROR_DEVICE_LOST;
    }

    zx::event image_release_fence;
    status = release_fence.duplicate(ZX_RIGHT_SAME_RIGHTS, &image_release_fence);
    if (status != ZX_OK) {
      fprintf(stderr,
              "failed to duplicate release fence, "
              "zx::event::duplicate() failed with status %d",
              status);
      return VK_ERROR_DEVICE_LOST;
    }

    pending_images_.push_back({std::move(image_release_fence), index});

    std::vector<zx::event> acquire_fences;
    acquire_fences.push_back(std::move(acquire_fence));

    std::vector<zx::event> release_fences;
    release_fences.push_back(std::move(release_fence));

    TRACE_DURATION("gfx", "ImagePipeSwapchain::Present", "swapchain_image_index", index, "image_id",
                   images_[index].id);
    surface()->PresentImage(images_[index].id, std::move(acquire_fences),
                            std::move(release_fences));
  }

  return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL QueuePresentKHR(VkQueue queue,
                                               const VkPresentInfoKHR* pPresentInfo) {
  for (uint32_t i = 0; i < pPresentInfo->swapchainCount; i++) {
    auto swapchain = reinterpret_cast<ImagePipeSwapchain*>(pPresentInfo->pSwapchains[i]);
    VkResult result =
        swapchain->Present(queue, pPresentInfo->pImageIndices[i], pPresentInfo->waitSemaphoreCount,
                           pPresentInfo->pWaitSemaphores);
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
                                                                  VkBool32* pSupported) {
  *pSupported = surface != nullptr ? VK_TRUE : VK_FALSE;
  return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL CreateImagePipeSurfaceFUCHSIA(
    VkInstance instance, const VkImagePipeSurfaceCreateInfoFUCHSIA* pCreateInfo,
    const VkAllocationCallbacks* pAllocator, VkSurfaceKHR* pSurface) {
  auto out_surface =
#if USE_IMAGEPIPE_SURFACE_FB
      std::make_unique<ImagePipeSurfaceDisplay>();
#else
      std::make_unique<ImagePipeSurfaceAsync>(pCreateInfo->imagePipeHandle);
#endif

  if (!out_surface->Init()) {
    return VK_ERROR_DEVICE_LOST;
  }

  *pSurface = reinterpret_cast<VkSurfaceKHR>(out_surface.release());
  return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL DestroySurfaceKHR(VkInstance instance, VkSurfaceKHR surface,
                                             const VkAllocationCallbacks* pAllocator) {
  delete reinterpret_cast<ImagePipeSurface*>(surface);
}

VKAPI_ATTR VkResult VKAPI_CALL
GetPhysicalDeviceSurfaceCapabilitiesKHR(VkPhysicalDevice physicalDevice, VkSurfaceKHR surface,
                                        VkSurfaceCapabilitiesKHR* pSurfaceCapabilities) {
  VkLayerInstanceDispatchTable* instance_dispatch_table =
      GetLayerDataPtr(get_dispatch_key(physicalDevice), layer_data_map)
          ->instance_dispatch_table.get();

  VkPhysicalDeviceProperties props;
  instance_dispatch_table->GetPhysicalDeviceProperties(physicalDevice, &props);

  pSurfaceCapabilities->minImageCount = 2;
  pSurfaceCapabilities->maxImageCount = 0;
  pSurfaceCapabilities->minImageExtent = {1, 1};

  auto image_pipe_surface = reinterpret_cast<ImagePipeSurface*>(surface);

  uint32_t width = 0;
  uint32_t height = 0;
  if (image_pipe_surface->GetSize(&width, &height)) {
    pSurfaceCapabilities->maxImageExtent = {width, height};
    pSurfaceCapabilities->currentExtent = pSurfaceCapabilities->maxImageExtent;
  } else {
    pSurfaceCapabilities->currentExtent = {0xFFFFFFFF, 0xFFFFFFFF};
    pSurfaceCapabilities->maxImageExtent = {props.limits.maxImageDimension2D,
                                            props.limits.maxImageDimension2D};
  }

  pSurfaceCapabilities->supportedTransforms = VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR;
  pSurfaceCapabilities->currentTransform = VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR;
  pSurfaceCapabilities->maxImageArrayLayers = 1;
  pSurfaceCapabilities->supportedUsageFlags = image_pipe_surface->SupportedUsage();
  pSurfaceCapabilities->supportedCompositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
  return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL
GetPhysicalDeviceSurfaceFormatsKHR(VkPhysicalDevice physicalDevice, const VkSurfaceKHR surface,
                                   uint32_t* pCount, VkSurfaceFormatKHR* pSurfaceFormats) {
  SupportedImageProperties& supported_properties =
      reinterpret_cast<ImagePipeSurface*>(surface)->GetSupportedImageProperties();
  if (pSurfaceFormats == nullptr) {
    *pCount = supported_properties.formats.size();
    return VK_SUCCESS;
  }

  assert(*pCount >= supported_properties.formats.size());
  memcpy(pSurfaceFormats, supported_properties.formats.data(),
         supported_properties.formats.size() * sizeof(VkSurfaceFormatKHR));
  *pCount = supported_properties.formats.size();
  return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL
GetPhysicalDeviceSurfacePresentModesKHR(VkPhysicalDevice physicalDevice, const VkSurfaceKHR surface,
                                        uint32_t* pCount, VkPresentModeKHR* pPresentModes) {
  constexpr int present_mode_count = 1;
  constexpr VkPresentModeKHR present_modes[] = {VK_PRESENT_MODE_FIFO_KHR};
  if (pPresentModes == nullptr) {
    *pCount = present_mode_count;
    return VK_SUCCESS;
  }
  VkResult result = VK_SUCCESS;
  if (*pCount < present_mode_count) {
    result = VK_INCOMPLETE;
  } else {
    *pCount = present_mode_count;
  }

  memcpy(pPresentModes, present_modes, *pCount * sizeof(VkPresentModeKHR));
  return result;
}

VKAPI_ATTR VkResult VKAPI_CALL CreateInstance(const VkInstanceCreateInfo* pCreateInfo,
                                              const VkAllocationCallbacks* pAllocator,
                                              VkInstance* pInstance) {
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
  LayerData* my_data = GetLayerDataPtr(instance_key, layer_data_map);

  my_data->instance_dispatch_table->DestroyInstance(instance, pAllocator);

  // Remove from |layer_data_map| and free LayerData struct.
  FreeLayerDataPtr(instance_key, layer_data_map);
}

VKAPI_ATTR VkResult VKAPI_CALL CreateDevice(VkPhysicalDevice gpu,
                                            const VkDeviceCreateInfo* pCreateInfo,
                                            const VkAllocationCallbacks* pAllocator,
                                            VkDevice* pDevice) {
  void* gpu_key = get_dispatch_key(gpu);
  LayerData* gpu_layer_data = GetLayerDataPtr(gpu_key, layer_data_map);

  bool external_memory_extension_available = false;
  bool external_semaphore_extension_available = false;
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
        if (!strcmp(VK_FUCHSIA_EXTERNAL_MEMORY_EXTENSION_NAME,
                    device_extensions[i].extensionName)) {
          external_memory_extension_available = true;
        }
        if (!strcmp(VK_FUCHSIA_EXTERNAL_SEMAPHORE_EXTENSION_NAME,
                    device_extensions[i].extensionName)) {
          external_semaphore_extension_available = true;
        }
        if (!strcmp(VK_FUCHSIA_BUFFER_COLLECTION_EXTENSION_NAME,
                    device_extensions[i].extensionName)) {
          fuchsia_buffer_collection_extension_available = true;
        }
      }
    }
  }
  if (!external_memory_extension_available) {
    fprintf(stderr, "Device extension not available: %s\n",
            VK_FUCHSIA_EXTERNAL_MEMORY_EXTENSION_NAME);
  }
  if (!external_semaphore_extension_available) {
    fprintf(stderr, "Device extension not available: %s\n",
            VK_FUCHSIA_EXTERNAL_SEMAPHORE_EXTENSION_NAME);
  }
  if (!fuchsia_buffer_collection_extension_available) {
    fprintf(stderr, "Device extension not available: %s\n",
            VK_FUCHSIA_BUFFER_COLLECTION_EXTENSION_NAME);
  }
  if (!external_memory_extension_available || !external_semaphore_extension_available)
    return VK_ERROR_INITIALIZATION_FAILED;

  VkDeviceCreateInfo create_info = *pCreateInfo;
  std::vector<const char*> enabled_extensions;
  for (uint32_t i = 0; i < create_info.enabledExtensionCount; i++) {
    enabled_extensions.push_back(create_info.ppEnabledExtensionNames[i]);
  }
  enabled_extensions.push_back(VK_FUCHSIA_EXTERNAL_MEMORY_EXTENSION_NAME);
  enabled_extensions.push_back(VK_FUCHSIA_EXTERNAL_SEMAPHORE_EXTENSION_NAME);
  enabled_extensions.push_back(VK_FUCHSIA_BUFFER_COLLECTION_EXTENSION_NAME);
  create_info.enabledExtensionCount = enabled_extensions.size();
  create_info.ppEnabledExtensionNames = enabled_extensions.data();

  VkLayerDeviceCreateInfo* chain_info = get_chain_info(pCreateInfo, VK_LAYER_LINK_INFO);

  assert(chain_info->u.pLayerInfo);
  PFN_vkGetInstanceProcAddr fpGetInstanceProcAddr =
      chain_info->u.pLayerInfo->pfnNextGetInstanceProcAddr;
  PFN_vkGetDeviceProcAddr fpGetDeviceProcAddr = chain_info->u.pLayerInfo->pfnNextGetDeviceProcAddr;
  PFN_vkCreateDevice fpCreateDevice =
      (PFN_vkCreateDevice)fpGetInstanceProcAddr(gpu_layer_data->instance, "vkCreateDevice");
  if (fpCreateDevice == NULL) {
    return VK_ERROR_INITIALIZATION_FAILED;
  }

  // Advance the link info for the next element on the chain
  chain_info->u.pLayerInfo = chain_info->u.pLayerInfo->pNext;

  result = fpCreateDevice(gpu, &create_info, pAllocator, pDevice);
  if (result != VK_SUCCESS) {
    return result;
  }

  LayerData* device_layer_data = GetLayerDataPtr(get_dispatch_key(*pDevice), layer_data_map);

  // Setup device dispatch table
  device_layer_data->device_dispatch_table = std::make_unique<VkLayerDispatchTable>();
  device_layer_data->instance = gpu_layer_data->instance;
  layer_init_device_dispatch_table(*pDevice, device_layer_data->device_dispatch_table.get(),
                                   fpGetDeviceProcAddr);

  return result;
}

VKAPI_ATTR void VKAPI_CALL DestroyDevice(VkDevice device, const VkAllocationCallbacks* pAllocator) {
  dispatch_key device_key = get_dispatch_key(device);
  LayerData* device_data = GetLayerDataPtr(device_key, layer_data_map);
  device_data->device_dispatch_table->DestroyDevice(device, pAllocator);

  // Remove from |layer_data_map| and free LayerData struct.
  FreeLayerDataPtr(device_key, layer_data_map);
}

VKAPI_ATTR VkResult VKAPI_CALL EnumerateInstanceLayerProperties(uint32_t* pCount,
                                                                VkLayerProperties* pProperties) {
  return util_GetLayerProperties(1, &swapchain_layer, pCount, pProperties);
}

VKAPI_ATTR VkResult VKAPI_CALL EnumerateDeviceLayerProperties(VkPhysicalDevice physicalDevice,
                                                              uint32_t* pCount,
                                                              VkLayerProperties* pProperties) {
  return util_GetLayerProperties(1, &swapchain_layer, pCount, pProperties);
}

VKAPI_ATTR VkResult VKAPI_CALL EnumerateInstanceExtensionProperties(
    const char* pLayerName, uint32_t* pCount, VkExtensionProperties* pProperties) {
  if (pLayerName && !strcmp(pLayerName, swapchain_layer.layerName))
    return util_GetExtensionProperties(ARRAY_SIZE(instance_extensions), instance_extensions, pCount,
                                       pProperties);

  return VK_ERROR_LAYER_NOT_PRESENT;
}

VKAPI_ATTR VkResult VKAPI_CALL
EnumerateDeviceExtensionProperties(VkPhysicalDevice physicalDevice, const char* pLayerName,
                                   uint32_t* pCount, VkExtensionProperties* pProperties) {
  if (pLayerName && !strcmp(pLayerName, swapchain_layer.layerName))
    return util_GetExtensionProperties(ARRAY_SIZE(device_extensions), device_extensions, pCount,
                                       pProperties);

  assert(physicalDevice);

  dispatch_key key = get_dispatch_key(physicalDevice);
  LayerData* my_data = GetLayerDataPtr(key, layer_data_map);
  return my_data->instance_dispatch_table->EnumerateDeviceExtensionProperties(physicalDevice, NULL,
                                                                              pCount, pProperties);
}

VKAPI_ATTR VkResult VKAPI_CALL CreateDebugUtilsMessengerEXT(
    VkInstance instance, const VkDebugUtilsMessengerCreateInfoEXT* pCreateInfo,
    const VkAllocationCallbacks* pAllocator, VkDebugUtilsMessengerEXT* pMessenger) {
  dispatch_key key = get_dispatch_key(instance);
  LayerData* my_data = GetLayerDataPtr(key, layer_data_map);
  VkResult res = my_data->instance_dispatch_table->CreateDebugUtilsMessengerEXT(
      instance, pCreateInfo, pAllocator, pMessenger);
  if (res == VK_SUCCESS) {
    my_data->debug_callbacks[*pMessenger] = *pCreateInfo;
  }
  return res;
}

VKAPI_ATTR void VKAPI_CALL DestroyDebugUtilsMessengerEXT(VkInstance instance,
                                                         VkDebugUtilsMessengerEXT messenger,
                                                         const VkAllocationCallbacks* pAllocator) {
  dispatch_key key = get_dispatch_key(instance);
  LayerData* my_data = GetLayerDataPtr(key, layer_data_map);
  my_data->debug_callbacks.erase(messenger);
}

VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL GetDeviceProcAddr(VkDevice device, const char* funcName);
VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL GetInstanceProcAddr(VkInstance instance,
                                                             const char* funcName);

static inline PFN_vkVoidFunction layer_intercept_proc(const char* name) {
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
  if (!strcmp(name, "CreateDebugUtilsMessengerEXT"))
    return reinterpret_cast<PFN_vkVoidFunction>(CreateDebugUtilsMessengerEXT);
  if (!strcmp(name, "DestroyDebugUtilsMessengerEXT"))
    return reinterpret_cast<PFN_vkVoidFunction>(DestroyDebugUtilsMessengerEXT);
  return NULL;
}

static inline PFN_vkVoidFunction layer_intercept_instance_proc(const char* name) {
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
  if (!strcmp("CreateImagePipeSurfaceFUCHSIA", name))
    return reinterpret_cast<PFN_vkVoidFunction>(CreateImagePipeSurfaceFUCHSIA);
  if (!strcmp("DestroySurfaceKHR", name))
    return reinterpret_cast<PFN_vkVoidFunction>(DestroySurfaceKHR);
  if (!strcmp(name, "CreateDebugUtilsMessengerEXT"))
    return reinterpret_cast<PFN_vkVoidFunction>(CreateDebugUtilsMessengerEXT);
  if (!strcmp(name, "DestroyDebugUtilsMessengerEXT"))
    return reinterpret_cast<PFN_vkVoidFunction>(DestroyDebugUtilsMessengerEXT);
  return NULL;
}

VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL GetDeviceProcAddr(VkDevice device, const char* funcName) {
  PFN_vkVoidFunction addr;
  LayerData* dev_data;

  assert(device);

  addr = layer_intercept_proc(funcName);
  if (addr) {
    return addr;
  }

  dev_data = GetLayerDataPtr(get_dispatch_key(device), layer_data_map);

  VkLayerDispatchTable* pTable = dev_data->device_dispatch_table.get();

  if (pTable->GetDeviceProcAddr == NULL)
    return NULL;
  return pTable->GetDeviceProcAddr(device, funcName);
}

VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL GetInstanceProcAddr(VkInstance instance,
                                                             const char* funcName) {
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

  VkLayerInstanceDispatchTable* pTable = my_data->instance_dispatch_table.get();
  if (pTable->GetInstanceProcAddr == NULL) {
    return NULL;
  }
  addr = pTable->GetInstanceProcAddr(instance, funcName);
  return addr;
}

VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL GetPhysicalDeviceProcAddr(VkInstance instance,
                                                                   const char* funcName) {
  assert(instance);

  LayerData* my_data;
  my_data = GetLayerDataPtr(get_dispatch_key(instance), layer_data_map);
  VkLayerInstanceDispatchTable* pTable = my_data->instance_dispatch_table.get();

  if (pTable->GetPhysicalDeviceProcAddr == NULL)
    return NULL;
  return pTable->GetPhysicalDeviceProcAddr(instance, funcName);
}

}  // namespace image_pipe_swapchain

VK_LAYER_EXPORT VKAPI_ATTR VkResult VKAPI_CALL vkEnumerateInstanceExtensionProperties(
    const char* pLayerName, uint32_t* pCount, VkExtensionProperties* pProperties) {
  return image_pipe_swapchain::EnumerateInstanceExtensionProperties(pLayerName, pCount,
                                                                    pProperties);
}

VK_LAYER_EXPORT VKAPI_ATTR VkResult VKAPI_CALL
vkEnumerateInstanceLayerProperties(uint32_t* pCount, VkLayerProperties* pProperties) {
  return image_pipe_swapchain::EnumerateInstanceLayerProperties(pCount, pProperties);
}

VK_LAYER_EXPORT VKAPI_ATTR VkResult VKAPI_CALL vkEnumerateDeviceLayerProperties(
    VkPhysicalDevice physicalDevice, uint32_t* pCount, VkLayerProperties* pProperties) {
  assert(physicalDevice == VK_NULL_HANDLE);
  return image_pipe_swapchain::EnumerateDeviceLayerProperties(VK_NULL_HANDLE, pCount, pProperties);
}

VK_LAYER_EXPORT VKAPI_ATTR VkResult VKAPI_CALL
vkEnumerateDeviceExtensionProperties(VkPhysicalDevice physicalDevice, const char* pLayerName,
                                     uint32_t* pCount, VkExtensionProperties* pProperties) {
  assert(physicalDevice == VK_NULL_HANDLE);
  return image_pipe_swapchain::EnumerateDeviceExtensionProperties(VK_NULL_HANDLE, pLayerName,
                                                                  pCount, pProperties);
}

VK_LAYER_EXPORT VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL vkGetDeviceProcAddr(VkDevice dev,
                                                                             const char* funcName) {
  return image_pipe_swapchain::GetDeviceProcAddr(dev, funcName);
}

VK_LAYER_EXPORT VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL
vkGetInstanceProcAddr(VkInstance instance, const char* funcName) {
  return image_pipe_swapchain::GetInstanceProcAddr(instance, funcName);
}
