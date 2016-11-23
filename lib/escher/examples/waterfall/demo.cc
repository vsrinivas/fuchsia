// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "demo.h"

#include "escher/renderer/image.h"
#include "ftl/logging.h"
#include "ftl/memory/ref_ptr.h"

#include <iostream>
#include <set>

#define VK_CHECK_RESULT(XXX) FTL_CHECK(XXX.result == vk::Result::eSuccess)

// Helper for Demo::InitGlfw().
static void DemoGlfwErrorCallback(int err_code, const char* err_desc) {
  std::cerr << "GLFW ERROR: " << err_code << " " << err_desc << std::endl;
}

void Demo::InitGlfw() {
  FTL_CHECK(glfwInit());
  glfwSetErrorCallback(DemoGlfwErrorCallback);
}

// Helper for Demo::CreateInstance().
static std::vector<vk::LayerProperties> GetRequiredInstanceLayers(
    std::set<std::string> required_layer_names) {
  // Get list of all available layers.
  auto result = vk::enumerateInstanceLayerProperties();
  VK_CHECK_RESULT(result);
  std::vector<vk::LayerProperties>& props = result.value;

  // Keep only the required layers.  Panic if any are not available.
  std::vector<vk::LayerProperties> required_layers;
  for (auto& name : required_layer_names) {
    auto found = std::find_if(props.begin(), props.end(),
                              [&name](vk::LayerProperties& layer) {
                                return !strcmp(layer.layerName, name.c_str());
                              });
    FTL_CHECK(found != props.end());
    required_layers.push_back(*found);
  }
  return required_layers;
}

// Helper for Demo::CreateInstance().
static std::vector<vk::ExtensionProperties> GetRequiredInstanceExtensions(
    std::set<std::string> required_extension_names) {
  // Get list of all available extensions.
  auto result = vk::enumerateInstanceExtensionProperties();
  VK_CHECK_RESULT(result);
  std::vector<vk::ExtensionProperties>& props = result.value;

  // Keep only the required extensions.  Panic if any are not available.
  std::vector<vk::ExtensionProperties> required_extensions;
  for (auto& name : required_extension_names) {
    auto found =
        std::find_if(props.begin(), props.end(),
                     [&name](vk::ExtensionProperties& extension) {
                       return !strcmp(extension.extensionName, name.c_str());
                     });
    FTL_CHECK(found != props.end());
    required_extensions.push_back(*found);
  }
  return required_extensions;
}

void Demo::CreateInstance(InstanceParams params) {
  // Add our own required layers and extensions in addition to those provided
  // by the caller.  Verify that they are all available, and obtain info about
  // them that is used:
  // - to create the instance.
  // - for future reference.
  {
    instance_layers_ = GetRequiredInstanceLayers(
        // Duplicates are not allowed.
        std::set<std::string>(params.layer_names.begin(),
                              params.layer_names.end()));

    // Get names of extensions required by GLFW.
    uint32_t extensions_count;
    const char** extensions =
        glfwGetRequiredInstanceExtensions(&extensions_count);
    for (uint32_t i = 0; i < extensions_count; ++i) {
      params.extension_names.emplace_back(std::string(extensions[i]));
    }

    // We need this extension for getting debug callbacks.
    params.extension_names.emplace_back("VK_EXT_debug_report");

    instance_extensions_ = GetRequiredInstanceExtensions(
        // Duplicates are not allowed.
        std::set<std::string>(params.extension_names.begin(),
                              params.extension_names.end()));
  }

  // Create Vulkan instance.
  {
    // Gather names of layers/extensions to populate InstanceCreateInfo.
    std::vector<const char*> layer_names;
    for (auto& layer : instance_layers_) {
      layer_names.push_back(layer.layerName);
    }
    std::vector<const char*> extension_names;
    for (auto& extension : instance_extensions_) {
      extension_names.push_back(extension.extensionName);
    }

    vk::InstanceCreateInfo info;
    info.enabledLayerCount = layer_names.size();
    info.ppEnabledLayerNames = layer_names.data();
    info.enabledExtensionCount = extension_names.size();
    info.ppEnabledExtensionNames = extension_names.data();

    auto result = vk::createInstance(info);
    VK_CHECK_RESULT(result);
    instance_ = result.value;
  }

  // Obtain instance-specific function pointers.
  instance_procs_ = InstanceProcAddrs(instance_);

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
    VkResult result = instance_procs_.CreateDebugReportCallbackEXT(
        instance_, &dbgCreateInfo, nullptr, &debug_report_callback_);
    FTL_CHECK(result == VK_SUCCESS);
  }
}

void Demo::CreateWindowAndSurface(const WindowParams& params) {
  glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
  window_ = glfwCreateWindow(params.width, params.height,
                             params.window_name.c_str(), NULL, NULL);
  FTL_CHECK(window_);

  VkSurfaceKHR surface;
  VkResult err = glfwCreateWindowSurface(instance_, window_, NULL, &surface);
  FTL_CHECK(!err);
  surface_ = surface;
}

void Demo::CreateDeviceAndQueue() {
  // Obtain list of physical devices.
  auto result = instance_.enumeratePhysicalDevices();
  VK_CHECK_RESULT(result);
  std::vector<vk::PhysicalDevice>& devices = result.value;

  // Iterate over physical devices until we find one that meets our needs.
  for (auto& physical_device : devices) {
    auto result = physical_device.enumerateDeviceExtensionProperties();
    VK_CHECK_RESULT(result);
    std::vector<vk::ExtensionProperties>& device_props = result.value;
    auto found_device =
        std::find_if(device_props.begin(), device_props.end(),
                     [](vk::ExtensionProperties& extension) {
                       return !strcmp(extension.extensionName,
                                      VK_KHR_SWAPCHAIN_EXTENSION_NAME);
                     });
    if (found_device != device_props.end()) {
      // We found a device with the necessary extension.  Now let's ensure that
      // it has a queue that supports graphics.
      auto queues = physical_device.getQueueFamilyProperties();
      auto desired_flags =
          vk::QueueFlagBits::eGraphics | vk::QueueFlagBits::eCompute;
      for (size_t i = 0; i < queues.size(); ++i) {
        if (desired_flags == (queues[i].queueFlags & desired_flags)) {
          // TODO: it is possible that there is no queue family that supports
          // both graphics/compute and present.  In this case, we would need a
          // separate present queue.  For now, just assert that there is a
          // single queue that meets our needs.
          {
            VkBool32 supports_present;
            auto result = instance_procs_.GetPhysicalDeviceSurfaceSupportKHR(
                physical_device, i, surface_, &supports_present);
            FTL_CHECK(result == VK_SUCCESS);
            FTL_CHECK(supports_present == VK_TRUE);
          }

          // We found an appropriate device!  Remember it, then create a
          // logical device.
          physical_device_ = physical_device;

          // We may only create one queue, or we may create an additional
          // transfer-only queue... see below.
          vk::DeviceQueueCreateInfo queue_info[2];
          queue_info[0] = vk::DeviceQueueCreateInfo();
          queue_info[0] = vk::DeviceQueueCreateInfo();
          queue_info[0].queueFamilyIndex = i;
          queue_info[0].queueCount = 1;
          float queue_priorities[1] = {0.0};
          queue_info[0].pQueuePriorities = queue_priorities;

          vk::DeviceCreateInfo device_info;
          device_info.queueCreateInfoCount = 1;
          device_info.pQueueCreateInfos = queue_info;
          // TODO: need other device extensions?
          const char* swapchain_extension_name = "VK_KHR_swapchain";
          device_info.enabledExtensionCount = 1;
          device_info.ppEnabledExtensionNames = &swapchain_extension_name;

          // Try to find a transfer-only queue... if it exists, it will be the
          // fastest way to upload data to the GPU.
          for (size_t j = 0; j < queues.size(); ++j) {
            auto flags = queues[j].queueFlags;
            if (!(flags & vk::QueueFlagBits::eGraphics) &&
                !(flags & vk::QueueFlagBits::eCompute) &&
                (flags & vk::QueueFlagBits::eTransfer)) {
              // Found a transfer-only queue.  Update the parameters that will
              // be used to create the logical device.
              device_info.queueCreateInfoCount = 2;
              queue_info[1].queueFamilyIndex = j;
              queue_info[1].queueCount = 1;
              // TODO: make transfer queue higher priority?  Maybe unnecessary,
              // since we'll use semaphores to block the graphics/compute queue
              // until necessary transfers are complete.
              queue_info[1].pQueuePriorities = queue_priorities;
              break;
            }
          }

          // Create the logical device.
          auto result = physical_device_.createDevice(device_info);
          VK_CHECK_RESULT(result);
          device_ = result.value;

          // Obtain device-specific function pointers.
          device_procs_ = DeviceProcAddrs(device_);

          // Obtain the queues that we requested to be created with the device.
          queue_family_index_ = queue_info[0].queueFamilyIndex;
          queue_ = device_.getQueue(queue_family_index_, 0);
          if (device_info.queueCreateInfoCount == 2) {
            transfer_queue_family_index_ = queue_info[1].queueFamilyIndex;
            transfer_queue_ = device_.getQueue(transfer_queue_family_index_, 0);
          } else {
            transfer_queue_family_index_ = UINT32_MAX;
            transfer_queue_ = nullptr;
          }

          return;
        }
      }
    }
  }
  FTL_CHECK(false);
}

void Demo::CreateSwapchain(const WindowParams& window_params) {
  FTL_CHECK(!swapchain_.swapchain);
  FTL_CHECK(swapchain_.images.empty());
  FTL_CHECK(swapchain_.image_views.empty());

  vk::SurfaceCapabilitiesKHR surface_caps;
  {
    auto result = physical_device_.getSurfaceCapabilitiesKHR(surface_);
    VK_CHECK_RESULT(result);
    surface_caps = std::move(result.value);
  }

  std::vector<vk::PresentModeKHR> present_modes;
  {
    auto result = physical_device_.getSurfacePresentModesKHR(surface_);
    VK_CHECK_RESULT(result);
    present_modes = std::move(result.value);
  }

  // TODO: handle undefined width/height.
  vk::Extent2D swapchain_extent = surface_caps.currentExtent;
  constexpr uint32_t VK_UNDEFINED_WIDTH_OR_HEIGHT = 0xFFFFFFFF;
  FTL_CHECK(swapchain_extent.width != VK_UNDEFINED_WIDTH_OR_HEIGHT);
  FTL_CHECK(swapchain_extent.height != VK_UNDEFINED_WIDTH_OR_HEIGHT);
  // TODO: adjust demo width/height to match surface caps, if they don't
  // already.

  // FIFO mode is always available, but we will try to find a more efficient
  // mode.
  vk::PresentModeKHR swapchain_present_mode = vk::PresentModeKHR::eFifo;
  for (auto& mode : present_modes) {
    if (mode == vk::PresentModeKHR::eMailbox) {
      // Best choice: lowest-latency non-tearing mode.
      swapchain_present_mode = vk::PresentModeKHR::eMailbox;
      break;
    }
  }

  // Determine number of images in the swapchain.
  swapchain_image_count_ = window_params.desired_swapchain_image_count;
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
    auto result = physical_device_.getSurfaceFormatsKHR(surface_);
    VK_CHECK_RESULT(result);
    for (auto& sf : result.value) {
      if (sf.colorSpace != color_space)
        continue;
      if (sf.format == vk::Format::eB8G8R8A8Srgb) {
        // eB8G8R8A8Srgb is our favorite!
        format = sf.format;
      } else if (format == vk::Format::eUndefined) {
        // Anything is better than eUndefined.
        format = sf.format;
      }
    }
  }
  FTL_CHECK(format != vk::Format::eUndefined);

  // TODO: old_swapchain will come into play (I think) when we support
  // resizing
  // the window.
  vk::SwapchainKHR old_swapchain = nullptr;

  // Create the swapchain.
  vk::SwapchainKHR swapchain;
  {
    vk::SwapchainCreateInfoKHR info;
    info.surface = surface_;
    info.minImageCount = swapchain_image_count_;
    info.imageFormat = format;
    info.imageColorSpace = color_space;
    info.imageExtent = swapchain_extent;
    info.imageArrayLayers = 1;  // TODO: what is this?
    // Using eTransferDst allows us to blit debug info onto the surface.
    info.imageUsage = vk::ImageUsageFlagBits::eColorAttachment |
                      vk::ImageUsageFlagBits::eTransferDst;
    info.queueFamilyIndexCount = 1;
    info.pQueueFamilyIndices = &queue_family_index_;
    info.preTransform = pre_transform;
    info.presentMode = swapchain_present_mode;
    info.oldSwapchain = old_swapchain;
    info.clipped = true;

    auto result = device_.createSwapchainKHR(info);
    VK_CHECK_RESULT(result);
    swapchain = result.value;
  }

  if (old_swapchain) {
    // Note: destroying the swapchain also cleans up all its associated
    // presentable images once the platform is done with them.
    device_.destroySwapchainKHR(old_swapchain);
  }

  // Obtain swapchain images and buffers.
  {
    auto result = device_.getSwapchainImagesKHR(swapchain);
    VK_CHECK_RESULT(result);

    std::vector<vk::Image> images(std::move(result.value));
    std::vector<escher::ImagePtr> escher_images;
    std::vector<vk::ImageView> image_views;
    image_views.reserve(images.size());
    escher_images.reserve(images.size());
    for (auto& im : images) {
      escher_images.push_back(ftl::MakeRefCounted<escher::Image>(
          im, format, swapchain_extent.width, swapchain_extent.height));

      vk::ImageSubresourceRange range;
      range.aspectMask = vk::ImageAspectFlagBits::eColor;
      range.levelCount = 1;
      range.layerCount = 1;

      vk::ImageViewCreateInfo info;
      info.viewType = vk::ImageViewType::e2D;
      info.format = format;
      info.subresourceRange = range;
      info.image = im;

      auto result = device_.createImageView(info);
      VK_CHECK_RESULT(result);

      image_views.push_back(result.value);
    }
    swapchain_ = escher::VulkanSwapchain(
        swapchain, escher_images, image_views, swapchain_extent.width,
        swapchain_extent.height, format, color_space);
  }
}

void Demo::DestroySwapchain() {
  for (auto& iview : swapchain_.image_views) {
    device_.destroyImageView(iview);
  }
  swapchain_.images.clear();
  swapchain_.image_views.clear();

  FTL_CHECK(swapchain_.swapchain);
  device_.destroySwapchainKHR(swapchain_.swapchain);
  swapchain_.swapchain = nullptr;
}

void Demo::DestroyDevice() {
  device_.destroy();
}

void Demo::DestroyInstance() {
  // Destroy the debug callback.  We use the C API here because we need to
  // dynamically load the destruction function.
  instance_procs_.DestroyDebugReportCallbackEXT(
      instance_, debug_report_callback_, nullptr);

  instance_.destroySurfaceKHR(surface_);
  instance_.destroy();
}

void Demo::ShutdownGlfw() {
  // TODO: close window, and... ?

  glfwTerminate();
}

VkBool32 Demo::HandleDebugReport(VkDebugReportFlagsEXT flags,
                                 VkDebugReportObjectTypeEXT objectType,
                                 uint64_t object,
                                 size_t location,
                                 int32_t messageCode,
                                 const char* pLayerPrefix,
                                 const char* pMessage) {
  std::cerr << "Vulkan Error: " << pMessage << " (from layer: " << pLayerPrefix
            << ")" << std::endl;
  return true;
}

escher::VulkanContext Demo::GetVulkanContext() {
  return escher::VulkanContext(instance_, physical_device_, device_, queue_,
                               queue_family_index_, transfer_queue_,
                               transfer_queue_family_index_);
}
