// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lib/escher/vk/vulkan_device_queues.h"

#include <set>

#include "lib/escher/impl/vulkan_utils.h"
#include "lib/fxl/logging.h"

namespace escher {

template <typename FuncT>
static FuncT GetDeviceProcAddr(vk::Device device, const char* func_name) {
  FuncT func = reinterpret_cast<FuncT>(device.getProcAddr(func_name));
  FXL_CHECK(func) << "failed to find function address for: " << func_name;
  return func;
}

#define GET_DEVICE_PROC_ADDR(XXX) \
  XXX = GetDeviceProcAddr<PFN_vk##XXX>(device, "vk" #XXX)

VulkanDeviceQueues::Caps::Caps(vk::PhysicalDeviceProperties props)
    : max_image_width(props.limits.maxImageDimension2D),
      max_image_height(props.limits.maxImageDimension2D) {}

VulkanDeviceQueues::ProcAddrs::ProcAddrs(
    vk::Device device, const std::set<std::string>& extension_names) {
  if (extension_names.find(VK_KHR_SWAPCHAIN_EXTENSION_NAME) !=
      extension_names.end()) {
    GET_DEVICE_PROC_ADDR(CreateSwapchainKHR);
    GET_DEVICE_PROC_ADDR(DestroySwapchainKHR);
    GET_DEVICE_PROC_ADDR(GetSwapchainImagesKHR);
    GET_DEVICE_PROC_ADDR(AcquireNextImageKHR);
    GET_DEVICE_PROC_ADDR(QueuePresentKHR);
  }
#if defined(OS_FUCHSIA)
  if (extension_names.find(VK_KHR_EXTERNAL_SEMAPHORE_FUCHSIA_EXTENSION_NAME) !=
      extension_names.end()) {
    GET_DEVICE_PROC_ADDR(ImportSemaphoreFuchsiaHandleKHR);
    GET_DEVICE_PROC_ADDR(GetSemaphoreFuchsiaHandleKHR);
    GET_DEVICE_PROC_ADDR(GetMemoryFuchsiaHandleKHR);
  }
#endif
}

#if defined(OS_FUCHSIA)
vk::ResultValueType<uint32_t>::type
VulkanDeviceQueues::ProcAddrs::getMemoryFuchsiaHandleKHR(
    const vk::Device& device,
    const vk::MemoryGetFuchsiaHandleInfoKHR& getFuchsiaHandleInfo) const {
  uint32_t fuchsiaHandle;
  vk::Result result = static_cast<vk::Result>(GetMemoryFuchsiaHandleKHR(
      device,
      reinterpret_cast<const VkMemoryGetFuchsiaHandleInfoKHR*>(
          &getFuchsiaHandleInfo),
      &fuchsiaHandle));
  return vk::createResultValue(result, fuchsiaHandle,
                               "vk::Device::getMemoryFuchsiaHandleKHR");
}
#endif

namespace {

// Return value for FindSuitablePhysicalDeviceAndQueueFamilies(). Valid if and
// only if device is non-null.  Otherwise, no suitable device was found.
struct SuitablePhysicalDeviceAndQueueFamilies {
  vk::PhysicalDevice physical_device;
  uint32_t main_queue_family;
  uint32_t transfer_queue_family;
};

SuitablePhysicalDeviceAndQueueFamilies
FindSuitablePhysicalDeviceAndQueueFamilies(
    const VulkanInstancePtr& instance,
    const VulkanDeviceQueues::Params& params) {
  auto physical_devices = ESCHER_CHECKED_VK_RESULT(
      instance->vk_instance().enumeratePhysicalDevices());

  // A suitable main queue needs to support graphics and compute.
  const auto kMainQueueFlags =
      vk::QueueFlagBits::eGraphics | vk::QueueFlagBits::eCompute;

  // A specialized transfer queue will only support transfer; see comment below,
  // where these flags are used.
  const auto kTransferQueueFlags = vk::QueueFlagBits::eTransfer |
                                   vk::QueueFlagBits::eGraphics |
                                   vk::QueueFlagBits::eCompute;

  for (auto& physical_device : physical_devices) {
    // Look for a physical device that has all required extensions.
    if (!VulkanDeviceQueues::ValidateExtensions(physical_device,
                                                params.extension_names)) {
      continue;
    }

    // Find the main queue family.  If none is found, continue on to the next
    // physical device.
    auto queues = physical_device.getQueueFamilyProperties();
    const bool filter_queues_for_present =
        params.surface &&
        !(params.flags &
          VulkanDeviceQueues::Params::kDisableQueueFilteringForPresent);
    for (size_t i = 0; i < queues.size(); ++i) {
      if (kMainQueueFlags == (queues[i].queueFlags & kMainQueueFlags)) {
        if (filter_queues_for_present) {
          // TODO: it is possible that there is no queue family that supports
          // both graphics/compute and present.  In this case, we would need a
          // separate present queue.  For now, just look for a single queue that
          // meets all of our needs.
          VkBool32 supports_present;
          auto result =
              instance->proc_addrs().GetPhysicalDeviceSurfaceSupportKHR(
                  physical_device, i, params.surface, &supports_present);
          FXL_CHECK(result == VK_SUCCESS);
          if (supports_present != VK_TRUE) {
            FXL_LOG(INFO)
                << "Queue supports graphics/compute, but not presentation";
            continue;
          }
        }

        // At this point, we have already succeeded.  Now, try to find the
        // optimal transfer queue family.
        SuitablePhysicalDeviceAndQueueFamilies result;
        result.physical_device = physical_device;
        result.main_queue_family = i;
        result.transfer_queue_family = i;
        for (size_t j = 0; j < queues.size(); ++j) {
          if ((queues[i].queueFlags & kTransferQueueFlags) ==
              vk::QueueFlagBits::eTransfer) {
            // We have found a transfer-only queue.  This is the fastest way to
            // upload data to the GPU.
            result.transfer_queue_family = j;
            break;
          }
        }
        return result;
      }
    }
  }
  return {vk::PhysicalDevice(), 0, 0};
}

}  // namespace

fxl::RefPtr<VulkanDeviceQueues> VulkanDeviceQueues::New(
    VulkanInstancePtr instance, Params params) {
  if (params.surface) {
    // If the params contain a surface, then ensure that the swapchain extension
    // is supported so that we can render to that surface.
    params.extension_names.insert(VK_KHR_SWAPCHAIN_EXTENSION_NAME);
  }
#if defined(OS_FUCHSIA)
  params.extension_names.insert(
      VK_KHR_EXTERNAL_SEMAPHORE_FUCHSIA_EXTENSION_NAME);
#endif

  vk::PhysicalDevice physical_device;
  uint32_t main_queue_family;
  uint32_t transfer_queue_family;
  {
    SuitablePhysicalDeviceAndQueueFamilies result =
        FindSuitablePhysicalDeviceAndQueueFamilies(instance, params);
    FXL_CHECK(result.physical_device)
        << "Unable to find a suitable physical device.";
    physical_device = result.physical_device;
    main_queue_family = result.main_queue_family;
    transfer_queue_family = result.transfer_queue_family;
  }

  // Prepare to create the Device and Queues.
  vk::DeviceQueueCreateInfo queue_info[2];
  const float kQueuePriority = 0;
  queue_info[0] = vk::DeviceQueueCreateInfo();
  queue_info[0].queueFamilyIndex = main_queue_family;
  queue_info[0].queueCount = 1;
  queue_info[0].pQueuePriorities = &kQueuePriority;
  queue_info[1] = vk::DeviceQueueCreateInfo();
  queue_info[1].queueFamilyIndex = transfer_queue_family;
  queue_info[1].queueCount = 1;
  queue_info[1].pQueuePriorities = &kQueuePriority;

  std::vector<const char*> extension_names;
  for (auto& extension : params.extension_names) {
    extension_names.push_back(extension.c_str());
  }

  // Specify the required physical device features, and verify that they are all
  // supported.
  // TODO(ES-111): instead of hard-coding the required features here, provide a
  // mechanism for Escher clients to specify additional required features.
  vk::PhysicalDeviceFeatures required_device_features;
  vk::PhysicalDeviceFeatures supported_device_features;
  physical_device.getFeatures(&supported_device_features);
  bool device_has_all_required_features = true;

  // TODO(MA-478): ADD_REQUIRED_FEATURE allows device to be created even if
  // 'shaderClipDistance' is not supported.  This is a short-term workaround;
  // Scenic will need this feature soon.
#define ADD_REQUIRED_FEATURE(X)                                               \
  required_device_features.X = true;                                          \
  if (!supported_device_features.X) {                                         \
    FXL_LOG(ERROR) << "Required Vulkan Device feature not supported: " << #X; \
    if (offsetof(vk::PhysicalDeviceFeatures, X) ==                            \
        offsetof(vk::PhysicalDeviceFeatures, shaderClipDistance)) {           \
      FXL_LOG(WARNING)                                                        \
          << "... TODO(MA-478): attempting to create Device anyway.";         \
      required_device_features.shaderClipDistance = false;                    \
    } else {                                                                  \
      device_has_all_required_features = false;                               \
      required_device_features.X = true;                                      \
    }                                                                         \
  }

  ADD_REQUIRED_FEATURE(shaderClipDistance);

  if (!device_has_all_required_features) {
    return fxl::RefPtr<VulkanDeviceQueues>();
  }
#undef ADD_REQUIRED_FEATURE

  // Almost ready to create the device; start populating the VkDeviceCreateInfo.
  vk::DeviceCreateInfo device_info;
  device_info.queueCreateInfoCount = 2;
  device_info.pQueueCreateInfos = queue_info;
  device_info.enabledExtensionCount = extension_names.size();
  device_info.ppEnabledExtensionNames = extension_names.data();
  device_info.pEnabledFeatures = &required_device_features;

  // It's possible that the main queue and transfer queue are in the same
  // queue family.  Adjust the device-creation parameters to account for this.
  uint32_t main_queue_index = 0;
  uint32_t transfer_queue_index = 0;
  if (main_queue_family == transfer_queue_family) {
#if 0
    // TODO: it may be worthwhile to create multiple queues in the same family.
    // However, we would need to look at VkQueueFamilyProperties.queueCount to
    // make sure that we can create multiple queues for that family.  For now,
    // it is easier to share a single queue when the main/transfer queues are in
    // the same family.
    queue_info[0].queueCount = 2;
    device_info.queueCreateInfoCount = 1;
    transfer_queue_index = 1;
#else
    device_info.queueCreateInfoCount = 1;
#endif
  }

  // Create the device.
  auto result = physical_device.createDevice(device_info);
  if (result.result != vk::Result::eSuccess) {
    FXL_LOG(WARNING) << "Could not create Vulkan Device.";
    return fxl::RefPtr<VulkanDeviceQueues>();
  }
  vk::Device device = result.value;

  // Obtain the queues that we requested to be created with the device.
  vk::Queue main_queue = device.getQueue(main_queue_family, main_queue_index);
  vk::Queue transfer_queue =
      device.getQueue(transfer_queue_family, transfer_queue_index);

  return fxl::AdoptRef(new VulkanDeviceQueues(
      device, physical_device, main_queue, main_queue_family, transfer_queue,
      transfer_queue_family, std::move(instance), std::move(params)));
}

VulkanDeviceQueues::VulkanDeviceQueues(
    vk::Device device, vk::PhysicalDevice physical_device, vk::Queue main_queue,
    uint32_t main_queue_family, vk::Queue transfer_queue,
    uint32_t transfer_queue_family, VulkanInstancePtr instance, Params params)
    : device_(device),
      physical_device_(physical_device),
      main_queue_(main_queue),
      main_queue_family_(main_queue_family),
      transfer_queue_(transfer_queue),
      transfer_queue_family_(transfer_queue_family),
      instance_(std::move(instance)),
      params_(std::move(params)),
      caps_(physical_device.getProperties()),
      proc_addrs_(device_, params_.extension_names) {}

VulkanDeviceQueues::~VulkanDeviceQueues() { device_.destroy(); }

bool VulkanDeviceQueues::ValidateExtensions(
    vk::PhysicalDevice device,
    const std::set<std::string>& required_extension_names) {
  auto extensions =
      ESCHER_CHECKED_VK_RESULT(device.enumerateDeviceExtensionProperties());

  for (auto& name : required_extension_names) {
    auto found =
        std::find_if(extensions.begin(), extensions.end(),
                     [&name](vk::ExtensionProperties& extension) {
                       return !strncmp(extension.extensionName, name.c_str(),
                                       VK_MAX_EXTENSION_NAME_SIZE);
                     });
    if (found == extensions.end()) {
      return false;
    }
  }
  return true;
}

VulkanContext VulkanDeviceQueues::GetVulkanContext() const {
  return escher::VulkanContext(instance_->vk_instance(), vk_physical_device(),
                               vk_device(), vk_main_queue(),
                               vk_main_queue_family(), vk_transfer_queue(),
                               vk_transfer_queue_family());
}

}  // namespace escher
