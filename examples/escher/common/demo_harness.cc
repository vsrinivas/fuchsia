// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/examples/escher/common/demo_harness.h"

#include "garnet/examples/escher/common/demo.h"
#include "lib/escher/escher_process_init.h"
#include "lib/escher/vk/gpu_mem.h"
#include "lib/escher/vk/image.h"
#include "lib/escher/vk/vulkan_instance.h"
#include "lib/fxl/logging.h"
#include "lib/fxl/memory/ref_ptr.h"

#include <iostream>
#include <set>

#define VK_CHECK_RESULT(XXX) FXL_CHECK(XXX.result == vk::Result::eSuccess)

DemoHarness::DemoHarness(WindowParams window_params)
    : window_params_(window_params) {
  // Init() is called by DemoHarness::New().
}

DemoHarness::~DemoHarness() { FXL_DCHECK(shutdown_complete_); }

void DemoHarness::Init(InstanceParams instance_params) {
  FXL_LOG(INFO) << "Initializing " << window_params_.window_name
                << (window_params_.use_fullscreen ? " (fullscreen "
                                                  : " (windowed ")
                << window_params_.width << "x" << window_params_.height << ")";
  InitWindowSystem();
  CreateInstance(std::move(instance_params));
  vk::SurfaceKHR surface = CreateWindowAndSurface(window_params_);
  CreateDeviceAndQueue({{}, surface});
  CreateSwapchain();
  escher::GlslangInitializeProcess();
}

void DemoHarness::Shutdown() {
  FXL_DCHECK(!shutdown_complete_);
  shutdown_complete_ = true;

  escher::GlslangFinalizeProcess();
  DestroySwapchain();
  DestroyDevice();
  DestroyInstance();
  ShutdownWindowSystem();
}

void DemoHarness::CreateInstance(InstanceParams params) {
  // Add our own required layers and extensions in addition to those provided
  // by the caller.  Verify that they are all available, and obtain info about
  // them that is used:
  // - to create the instance.
  // - for future reference.
  AppendPlatformSpecificInstanceExtensionNames(&params);

  // We need this extension for getting debug callbacks.
  params.extension_names.insert("VK_EXT_debug_report");

  instance_ = escher::VulkanInstance::New(std::move(params));
  FXL_CHECK(instance_);

  // Set up debug callback.
  {
    VkDebugReportCallbackCreateInfoEXT dbgCreateInfo;
    dbgCreateInfo.sType = VK_STRUCTURE_TYPE_DEBUG_REPORT_CREATE_INFO_EXT;
    dbgCreateInfo.pNext = NULL;
    dbgCreateInfo.pfnCallback = RedirectDebugReport;
    dbgCreateInfo.pUserData = this;
    dbgCreateInfo.flags = VK_DEBUG_REPORT_ERROR_BIT_EXT |
                          VK_DEBUG_REPORT_WARNING_BIT_EXT |
                          VK_DEBUG_REPORT_PERFORMANCE_WARNING_BIT_EXT;

    // We use the C API here due to dynamically loading the extension function.
    VkResult result = instance_proc_addrs().CreateDebugReportCallbackEXT(
        instance(), &dbgCreateInfo, nullptr, &debug_report_callback_);
    FXL_CHECK(result == VK_SUCCESS);
  }
}

void DemoHarness::CreateDeviceAndQueue(
    escher::VulkanDeviceQueues::Params params) {
  device_queues_ =
      escher::VulkanDeviceQueues::New(instance_, std::move(params));
}

void DemoHarness::CreateSwapchain() {
  FXL_CHECK(!swapchain_.swapchain);
  FXL_CHECK(swapchain_.images.empty());
  FXL_CHECK(!swapchain_image_owner_);
  swapchain_image_owner_ = std::make_unique<SwapchainImageOwner>();

  vk::SurfaceCapabilitiesKHR surface_caps;
  {
    auto result = physical_device().getSurfaceCapabilitiesKHR(surface());
    VK_CHECK_RESULT(result);
    surface_caps = std::move(result.value);
  }

  std::vector<vk::PresentModeKHR> present_modes;
  {
    auto result = physical_device().getSurfacePresentModesKHR(surface());
    VK_CHECK_RESULT(result);
    present_modes = std::move(result.value);
  }

  // TODO: handle undefined width/height.
  vk::Extent2D swapchain_extent = surface_caps.currentExtent;
  constexpr uint32_t VK_UNDEFINED_WIDTH_OR_HEIGHT = 0xFFFFFFFF;
  if (swapchain_extent.width == VK_UNDEFINED_WIDTH_OR_HEIGHT) {
    swapchain_extent.width = window_params_.width;
  }
  if (swapchain_extent.height == VK_UNDEFINED_WIDTH_OR_HEIGHT) {
    swapchain_extent.height = window_params_.height;
  }

  if (swapchain_extent.width != window_params_.width ||
      swapchain_extent.height != window_params_.height) {
    window_params_.width = swapchain_extent.width;
    window_params_.height = swapchain_extent.height;
  }
  FXL_CHECK(swapchain_extent.width == window_params_.width);
  FXL_CHECK(swapchain_extent.height == window_params_.height);

  // FIFO mode is always available, but we will try to find a more efficient
  // mode.
  vk::PresentModeKHR swapchain_present_mode = vk::PresentModeKHR::eFifo;
// TODO: Find out why these modes are causing lower performance on Skylake
#if 0
  for (auto& mode : present_modes) {
    if (mode == vk::PresentModeKHR::eMailbox) {
      // Best choice: lowest-latency non-tearing mode.
      swapchain_present_mode = vk::PresentModeKHR::eMailbox;
      break;
    }
    if (mode == vk::PresentModeKHR::eImmediate) {
      // Satisfactory choice: fastest, but tears.
      swapchain_present_mode = vk::PresentModeKHR::eImmediate;
    }
  }
#endif

  // Determine number of images in the swapchain.
  swapchain_image_count_ = window_params_.desired_swapchain_image_count;
  if (surface_caps.minImageCount > swapchain_image_count_) {
    swapchain_image_count_ = surface_caps.minImageCount;
  } else if (surface_caps.maxImageCount < swapchain_image_count_ &&
             surface_caps.maxImageCount != 0) {  // 0 means "no limit"
    swapchain_image_count_ = surface_caps.maxImageCount;
  }

  // TODO: choosing an appropriate pre-transform will probably be important on
  // mobile devices.
  auto pre_transform = vk::SurfaceTransformFlagBitsKHR::eIdentity;

  // Pick a format and color-space for the swap-chain.
  vk::Format format = vk::Format::eUndefined;
  vk::ColorSpaceKHR color_space = vk::ColorSpaceKHR::eSrgbNonlinear;
  {
    auto result = physical_device().getSurfaceFormatsKHR(surface());
    VK_CHECK_RESULT(result);
    for (auto& sf : result.value) {
      if (sf.colorSpace != color_space)
        continue;

      // TODO: remove this once Magma supports SRGB swapchains.
      if (sf.format == vk::Format::eB8G8R8A8Unorm) {
        format = sf.format;
        break;
      }

      if (sf.format == vk::Format::eB8G8R8A8Srgb) {
        // eB8G8R8A8Srgb is our favorite!
        format = sf.format;
        break;
      } else if (format == vk::Format::eUndefined) {
        // Anything is better than eUndefined.
        format = sf.format;
      }
    }
  }
  FXL_CHECK(format != vk::Format::eUndefined);

  // TODO: old_swapchain will come into play (I think) when we support
  // resizing the window.
  vk::SwapchainKHR old_swapchain = nullptr;

  // Using eTransferDst allows us to blit debug info onto the surface.
  // Using eSampled allows us to save memory by using the color attachment
  // for intermediate computation.
  const vk::ImageUsageFlags kImageUsageFlags =
      vk::ImageUsageFlagBits::eColorAttachment |
      vk::ImageUsageFlagBits::eTransferDst | vk::ImageUsageFlagBits::eSampled;

  // Create the swapchain.
  vk::SwapchainKHR swapchain;
  {
    vk::SwapchainCreateInfoKHR info;
    info.surface = surface();
    info.minImageCount = swapchain_image_count_;
    info.imageFormat = format;
    info.imageColorSpace = color_space;
    info.imageExtent = swapchain_extent;
    info.imageArrayLayers = 1;  // TODO: what is this?
    info.imageUsage = kImageUsageFlags;
    info.queueFamilyIndexCount = 1;
    uint32_t queue_family_index = device_queues_->vk_main_queue_family();
    info.pQueueFamilyIndices = &queue_family_index;
    info.preTransform = pre_transform;
    info.presentMode = swapchain_present_mode;
    info.oldSwapchain = old_swapchain;
    info.clipped = true;

    auto result = device().createSwapchainKHR(info);
    VK_CHECK_RESULT(result);
    swapchain = result.value;
  }

  if (old_swapchain) {
    // Note: destroying the swapchain also cleans up all its associated
    // presentable images once the platform is done with them.
    device().destroySwapchainKHR(old_swapchain);
  }

  // Obtain swapchain images and buffers.
  {
    auto result = device().getSwapchainImagesKHR(swapchain);
    VK_CHECK_RESULT(result);

    std::vector<vk::Image> images(std::move(result.value));
    std::vector<escher::ImagePtr> escher_images;
    escher_images.reserve(images.size());
    for (auto& im : images) {
      escher::ImageInfo image_info;
      image_info.format = format;
      image_info.width = swapchain_extent.width;
      image_info.height = swapchain_extent.height;
      image_info.usage = kImageUsageFlags;

      auto escher_image = escher::Image::New(swapchain_image_owner_.get(),
                                             image_info, im, nullptr);
      FXL_CHECK(escher_image);
      escher_images.push_back(escher_image);
    }
    swapchain_ = escher::VulkanSwapchain(
        swapchain, escher_images, swapchain_extent.width,
        swapchain_extent.height, format, color_space);
  }
}

void DemoHarness::DestroySwapchain() {
  swapchain_.images.clear();

  FXL_CHECK(swapchain_.swapchain);
  device().destroySwapchainKHR(swapchain_.swapchain);
  swapchain_.swapchain = nullptr;
}

void DemoHarness::DestroyDevice() {
  if (auto surf = surface()) {
    instance().destroySurfaceKHR(surf);
  }
  device_queues_ = nullptr;
}

void DemoHarness::DestroyInstance() {
  // Destroy the debug callback.  We use the C API here because we need to
  // dynamically load the destruction function.
  instance_proc_addrs().DestroyDebugReportCallbackEXT(
      instance(), debug_report_callback_, nullptr);

  instance_ = nullptr;
}

VkBool32 DemoHarness::HandleDebugReport(
    VkDebugReportFlagsEXT flags_in, VkDebugReportObjectTypeEXT object_type_in,
    uint64_t object, size_t location, int32_t message_code,
    const char* pLayerPrefix, const char* pMessage) {
  vk::DebugReportFlagsEXT flags(
      static_cast<vk::DebugReportFlagBitsEXT>(flags_in));
  vk::DebugReportObjectTypeEXT object_type(
      static_cast<vk::DebugReportObjectTypeEXT>(object_type_in));

  bool fatal = false;

  if (flags == vk::DebugReportFlagBitsEXT::eInformation) {
    // Paranoid check that there aren't multiple flags.
    FXL_DCHECK(flags == vk::DebugReportFlagBitsEXT::eInformation);

    std::cerr << "## Vulkan Information: ";
  } else if (flags == vk::DebugReportFlagBitsEXT::eWarning) {
    std::cerr << "## Vulkan Warning: ";
  } else if (flags == vk::DebugReportFlagBitsEXT::ePerformanceWarning) {
    std::cerr << "## Vulkan Performance Warning: ";
  } else if (flags == vk::DebugReportFlagBitsEXT::eError) {
    // Treat all errors as fatal.
    fatal = true;
    std::cerr << "## Vulkan Error: ";
  } else if (flags == vk::DebugReportFlagBitsEXT::eDebug) {
    std::cerr << "## Vulkan Debug: ";
  } else {
    // This should never happen, unless a new value has been added to
    // vk::DebugReportFlagBitsEXT.  In that case, add a new if-clause above.
    fatal = true;
    std::cerr << "## Vulkan Unknown Message Type (flags: "
              << vk::to_string(flags) << "): ";
  }

  std::cerr << pMessage << " (layer: " << pLayerPrefix
            << "  code: " << message_code
            << "  object-type: " << vk::to_string(object_type)
            << "  object: " << object << ")" << std::endl;

  // Crash immediately on fatal errors.
  FXL_CHECK(!fatal);

  return false;
}

escher::VulkanContext DemoHarness::GetVulkanContext() {
  return device_queues_->GetVulkanContext();
}

DemoHarness::SwapchainImageOwner::SwapchainImageOwner()
    : escher::ResourceManager(escher::EscherWeakPtr()) {}

void DemoHarness::SwapchainImageOwner::OnReceiveOwnable(
    std::unique_ptr<escher::Resource> resource) {
  FXL_DCHECK(resource->IsKindOf<escher::Image>());
  FXL_LOG(INFO) << "Destroying Image for swapchain image";
}
