// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/lib/escher/vk/vulkan_device_queues.h"

#include <lib/syslog/cpp/macros.h>

#include <set>

#include "src/ui/lib/escher/impl/vulkan_utils.h"

namespace escher {

VulkanDeviceQueues::Caps::Caps(vk::PhysicalDevice device) {
  {
    vk::PhysicalDeviceProperties props = device.getProperties();
    max_image_width = props.limits.maxImageDimension2D;
    max_image_height = props.limits.maxImageDimension2D;
    device_api_version = props.apiVersion;
    msaa_sample_counts =
        impl::GetSupportedColorSampleCounts(props.limits.sampledImageColorSampleCounts);
  }

  {
    auto formats = impl::GetSupportedDepthFormats(device, {
                                                              vk::Format::eD16Unorm,
                                                              vk::Format::eX8D24UnormPack32,
                                                              vk::Format::eD32Sfloat,
                                                              vk::Format::eS8Uint,
                                                              vk::Format::eD16UnormS8Uint,
                                                              vk::Format::eD24UnormS8Uint,
                                                              vk::Format::eD32SfloatS8Uint,
                                                          });
    depth_stencil_formats.insert(formats.begin(), formats.end());
  }
}

vk::ResultValue<vk::Format> VulkanDeviceQueues::Caps::GetMatchingDepthStencilFormat(
    const std::vector<vk::Format>& formats) const {
  for (auto fmt : formats) {
    if (depth_stencil_formats.find(fmt) != depth_stencil_formats.end()) {
      return {vk::Result::eSuccess, fmt};
    }
  }
  return {vk::Result::eErrorFeatureNotPresent, vk::Format::eUndefined};
}

vk::ResultValue<size_t> VulkanDeviceQueues::Caps::GetMatchingSampleCount(
    const std::vector<size_t>& counts) const {
  for (auto count : counts) {
    if (msaa_sample_counts.find(count) != msaa_sample_counts.end()) {
      return {vk::Result::eSuccess, count};
    }
  }
  return {vk::Result::eErrorFeatureNotPresent, 0};
}

std::set<vk::Format> VulkanDeviceQueues::Caps::GetAllMatchingDepthStencilFormats(
    const std::set<vk::Format>& formats) const {
  std::set<vk::Format> result;
  std::set_intersection(formats.begin(), formats.end(), depth_stencil_formats.begin(),
                        depth_stencil_formats.end(), std::inserter(result, result.begin()));
  return result;
}

std::set<size_t> VulkanDeviceQueues::Caps::GetAllMatchingSampleCounts(
    const std::set<size_t>& counts) const {
  std::set<size_t> result;
  std::set_intersection(counts.begin(), counts.end(), msaa_sample_counts.begin(),
                        msaa_sample_counts.end(), std::inserter(result, result.begin()));
  return result;
}

namespace {

// Helper for PopulateProcAddrs().
template <typename FuncT>
static FuncT GetDeviceProcAddr(vk::Device device, const char* func_name) {
  FuncT func = reinterpret_cast<FuncT>(device.getProcAddr(func_name));
  FX_CHECK(func) << "failed to find function address for: " << func_name;
  return func;
}

// Helper for VulkanDeviceQueues constructor.
VulkanDeviceQueues::ProcAddrs PopulateProcAddrs(vk::Device device,
                                                const VulkanDeviceQueues::Params& params) {
#define GET_DEVICE_PROC_ADDR(XXX) result.XXX = GetDeviceProcAddr<PFN_vk##XXX>(device, "vk" #XXX)

  VulkanDeviceQueues::ProcAddrs result;
  if (params.required_extension_names.find(VK_KHR_SWAPCHAIN_EXTENSION_NAME) !=
          params.required_extension_names.end() ||
      params.desired_extension_names.find(VK_KHR_SWAPCHAIN_EXTENSION_NAME) !=
          params.desired_extension_names.end()) {
    GET_DEVICE_PROC_ADDR(CreateSwapchainKHR);
    GET_DEVICE_PROC_ADDR(DestroySwapchainKHR);
    GET_DEVICE_PROC_ADDR(GetSwapchainImagesKHR);
    GET_DEVICE_PROC_ADDR(AcquireNextImageKHR);
    GET_DEVICE_PROC_ADDR(QueuePresentKHR);
  }
  return result;

#undef GET_DEVICE_PROC_ADDR
}

// Return value for FindSuitablePhysicalDeviceAndQueueFamilies(). Valid if and
// only if device is non-null.  Otherwise, no suitable device was found.
struct SuitablePhysicalDeviceAndQueueFamilies {
  vk::PhysicalDevice physical_device;
  uint32_t main_queue_family;
  uint32_t transfer_queue_family;
};

SuitablePhysicalDeviceAndQueueFamilies FindSuitablePhysicalDeviceAndQueueFamilies(
    const VulkanInstancePtr& instance, const VulkanDeviceQueues::Params& params) {
  auto physical_devices =
      ESCHER_CHECKED_VK_RESULT(instance->vk_instance().enumeratePhysicalDevices());

  // A suitable main queue needs to support graphics and compute.
  const auto kMainQueueFlags = vk::QueueFlagBits::eGraphics | vk::QueueFlagBits::eCompute;

  // A specialized transfer queue will only support transfer; see comment below,
  // where these flags are used.
  const auto kTransferQueueFlags =
      vk::QueueFlagBits::eTransfer | vk::QueueFlagBits::eGraphics | vk::QueueFlagBits::eCompute;

  for (auto& physical_device : physical_devices) {
    // Look for a physical device that has all required extensions.
    if (!VulkanDeviceQueues::ValidateExtensions(physical_device, params.required_extension_names,
                                                instance->params().layer_names)) {
      continue;
    }

    // Find the main queue family.  If none is found, continue on to the next
    // physical device.
    auto queues = physical_device.getQueueFamilyProperties();
    const bool filter_queues_for_present =
        params.surface &&
        !(params.flags & VulkanDeviceQueues::Params::kDisableQueueFilteringForPresent);
    for (uint32_t i = 0; i < queues.size(); ++i) {
      if (kMainQueueFlags == (queues[i].queueFlags & kMainQueueFlags)) {
        if (filter_queues_for_present) {
          // TODO: it is possible that there is no queue family that supports
          // both graphics/compute and present.  In this case, we would need a
          // separate present queue.  For now, just look for a single queue that
          // meets all of our needs.
          auto get_surface_support_result =
              physical_device.getSurfaceSupportKHR(i, params.surface, instance->proc_addrs());
          FX_CHECK(get_surface_support_result.result == vk::Result::eSuccess);
          VkBool32 supports_present = get_surface_support_result.value;
          if (supports_present != VK_TRUE) {
            FX_LOGS(INFO) << "Queue supports graphics/compute, but not presentation";
            continue;
          }
        }

        // At this point, we have already succeeded.  Now, try to find the
        // optimal transfer queue family.
        SuitablePhysicalDeviceAndQueueFamilies result;
        result.physical_device = physical_device;
        result.main_queue_family = i;
        result.transfer_queue_family = i;
        for (uint32_t j = 0; j < queues.size(); ++j) {
          if ((queues[i].queueFlags & kTransferQueueFlags) == vk::QueueFlagBits::eTransfer) {
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

// Helper for ValidateExtensions().
bool ValidateExtension(vk::PhysicalDevice device, const std::string name,
                       const std::vector<vk::ExtensionProperties>& base_extensions,
                       const std::set<std::string>& required_layer_names) {
  auto found = std::find_if(base_extensions.begin(), base_extensions.end(),
                            [&name](const vk::ExtensionProperties& extension) {
                              return !strncmp(extension.extensionName, name.c_str(),
                                              VK_MAX_EXTENSION_NAME_SIZE);
                            });
  if (found != base_extensions.end())
    return true;

  // Didn't find the extension in the base list of extensions.  Perhaps it is
  // implemented in a layer.
  for (auto& layer_name : required_layer_names) {
    auto layer_extensions =
        ESCHER_CHECKED_VK_RESULT(device.enumerateDeviceExtensionProperties(layer_name));
    FX_LOGS(INFO) << "Looking for Vulkan device extension: " << name << " in layer: " << layer_name;

    auto found = std::find_if(layer_extensions.begin(), layer_extensions.end(),
                              [&name](vk::ExtensionProperties& extension) {
                                return !strncmp(extension.extensionName, name.c_str(),
                                                VK_MAX_EXTENSION_NAME_SIZE);
                              });
    if (found != layer_extensions.end())
      return true;
  }

  return false;
}

}  // namespace

fxl::RefPtr<VulkanDeviceQueues> VulkanDeviceQueues::New(VulkanInstancePtr instance, Params params) {
  // Escher requires the memory_requirements_2 extension for the
  // vma_gpu_allocator to function.
  params.required_extension_names.insert(VK_KHR_GET_MEMORY_REQUIREMENTS_2_EXTENSION_NAME);

  // If the params contain a surface, then ensure that the swapchain extension
  // is supported so that we can render to that surface.
  if (params.surface) {
    params.required_extension_names.insert(VK_KHR_SWAPCHAIN_EXTENSION_NAME);
  }

#if defined(OS_FUCHSIA)
  // If we're running on Fuchsia, make sure we have our semaphore extensions.
  params.required_extension_names.insert(VK_FUCHSIA_EXTERNAL_SEMAPHORE_EXTENSION_NAME);
  params.required_extension_names.insert(VK_KHR_EXTERNAL_SEMAPHORE_EXTENSION_NAME);
#endif

  vk::PhysicalDevice physical_device;
  uint32_t main_queue_family;
  uint32_t transfer_queue_family;
  {
    SuitablePhysicalDeviceAndQueueFamilies result =
        FindSuitablePhysicalDeviceAndQueueFamilies(instance, params);
    FX_CHECK(result.physical_device) << "Unable to find a suitable physical device.";
    physical_device = result.physical_device;
    main_queue_family = result.main_queue_family;
    transfer_queue_family = result.transfer_queue_family;
  }

  // Partially populate device capabilities from the physical device.
  // Other stuff (e.g. which extensions are supported) will be added below.
  VulkanDeviceQueues::Caps caps(physical_device);

  auto physical_device_features = vk::PhysicalDeviceFeatures2();
  auto physical_device_memory_features = vk::PhysicalDeviceProtectedMemoryFeatures();
  auto sampler_ycbcr_conversion_features = vk::PhysicalDeviceSamplerYcbcrConversionFeatures();
  physical_device_features.setPNext(&physical_device_memory_features);
  physical_device_memory_features.setPNext(&sampler_ycbcr_conversion_features);

  physical_device.getFeatures2(&physical_device_features);

  // Get the maximum supported Vulkan API version (minimum of device version and instance version).
  uint32_t max_api_version = std::min(caps.device_api_version, instance->api_version());

  // Protected memory is only supported with Vulkan API version 1.1.
  if (!physical_device_memory_features.protectedMemory || max_api_version < VK_API_VERSION_1_1) {
    FX_LOGS(INFO) << "Protected memory is not supported.";
    caps.allow_protected_memory = false;
  } else {
    caps.allow_protected_memory = params.flags & VulkanDeviceQueues::Params::kAllowProtectedMemory;
  }

  // Prepare to create the Device and Queues.
  vk::DeviceQueueCreateInfo queue_info[2];
  const float kQueuePriority = 0;
  queue_info[0] = vk::DeviceQueueCreateInfo();
  queue_info[0].queueFamilyIndex = main_queue_family;
  queue_info[0].flags = caps.allow_protected_memory ? vk::DeviceQueueCreateFlagBits::eProtected
                                                    : vk::DeviceQueueCreateFlags();
  queue_info[0].queueCount = 1;
  queue_info[0].pQueuePriorities = &kQueuePriority;
  queue_info[1] = vk::DeviceQueueCreateInfo();
  queue_info[1].queueFamilyIndex = transfer_queue_family;
  queue_info[1].queueCount = 1;
  queue_info[1].pQueuePriorities = &kQueuePriority;

  // Prepare the list of extension names that will be used to create the device.
  {
    // These extensions were already validated by
    // FindSuitablePhysicalDeviceAndQueueFamilies();
    caps.extensions = params.required_extension_names;

    // Request as many of the desired (but optional) extensions as possible.
    auto extensions =
        ESCHER_CHECKED_VK_RESULT(physical_device.enumerateDeviceExtensionProperties());

    std::set<std::string> available_desired_extensions;
    for (auto& name : params.desired_extension_names) {
      if (ValidateExtension(physical_device, name, extensions, instance->params().layer_names)) {
        caps.extensions.insert(name);
      }
    }
  }
  std::vector<const char*> extension_names;
  for (auto& extension : caps.extensions) {
    extension_names.push_back(extension.c_str());
  }

  caps.allow_ycbcr =
      sampler_ycbcr_conversion_features.samplerYcbcrConversion &&
      caps.extensions.find(VK_KHR_SAMPLER_YCBCR_CONVERSION_EXTENSION_NAME) != caps.extensions.end();

  // Specify the required physical device features, and verify that they are all
  // supported.
  // TODO(fxbug.dev/7202): instead of hard-coding the required features here, provide a
  // mechanism for Escher clients to specify additional required features.
  vk::PhysicalDeviceFeatures supported_device_features;
  physical_device.getFeatures(&supported_device_features);
  bool device_has_all_required_features = true;

#define ADD_DESIRED_FEATURE(X)                                              \
  if (supported_device_features.X) {                                        \
    caps.enabled_features.X = true;                                         \
  } else {                                                                  \
    FX_LOGS(INFO) << "Desired Vulkan Device feature not supported: " << #X; \
  }

#define ADD_REQUIRED_FEATURE(X)                                               \
  caps.enabled_features.X = true;                                             \
  if (!supported_device_features.X) {                                         \
    FX_LOGS(ERROR) << "Required Vulkan Device feature not supported: " << #X; \
    device_has_all_required_features = false;                                 \
  }

  // TODO(fxbug.dev/13086): We would like to make 'shaderClipDistance' a requirement on
  // all Scenic platforms.  For now, treat it as a DESIRED_FEATURE.
  ADD_DESIRED_FEATURE(shaderClipDistance);
  ADD_DESIRED_FEATURE(fillModeNonSolid);

#undef ADD_DESIRED_FEATURE
#undef ADD_REQUIRED_FEATURE

  if (!device_has_all_required_features) {
    return fxl::RefPtr<VulkanDeviceQueues>();
  }

  // Almost ready to create the device; start populating the VkDeviceCreateInfo.
  vk::DeviceCreateInfo device_info;
  device_info.queueCreateInfoCount = 2;
  device_info.pQueueCreateInfos = queue_info;
  device_info.enabledExtensionCount = static_cast<uint32_t>(extension_names.size());
  device_info.ppEnabledExtensionNames = extension_names.data();
  device_info.pEnabledFeatures = &caps.enabled_features;
  if (caps.allow_protected_memory) {
    physical_device_memory_features.pNext = nullptr;
    device_info.pNext = &physical_device_memory_features;
  }
  if (caps.allow_ycbcr) {
    sampler_ycbcr_conversion_features.pNext = const_cast<void*>(device_info.pNext);
    device_info.pNext = &sampler_ycbcr_conversion_features;
  }

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
    FX_LOGS(WARNING) << "Could not create Vulkan Device: " << vk::to_string(result.result) << ".";
    return fxl::RefPtr<VulkanDeviceQueues>();
  }
  vk::Device device = result.value;

  // Obtain the queues that we requested to be created with the device.
  vk::Queue main_queue;
  if (caps.allow_protected_memory) {
    auto main_queue_info2 = vk::DeviceQueueInfo2()
                                .setFlags(vk::DeviceQueueCreateFlagBits::eProtected)
                                .setQueueFamilyIndex(main_queue_family)
                                .setQueueIndex(main_queue_index);
    main_queue = device.getQueue2(main_queue_info2);
  } else {
    main_queue = device.getQueue(main_queue_family, main_queue_index);
  }

  vk::Queue transfer_queue;
  if (main_queue_family == transfer_queue_family && main_queue_index == transfer_queue_index) {
    transfer_queue = main_queue;
  } else {
    transfer_queue = device.getQueue(transfer_queue_family, transfer_queue_index);
  }

  return fxl::AdoptRef(new VulkanDeviceQueues(
      device, physical_device, main_queue, main_queue_family, transfer_queue, transfer_queue_family,
      std::move(instance), std::move(params), std::move(caps)));
}

VulkanDeviceQueues::VulkanDeviceQueues(vk::Device device, vk::PhysicalDevice physical_device,
                                       vk::Queue main_queue, uint32_t main_queue_family,
                                       vk::Queue transfer_queue, uint32_t transfer_queue_family,
                                       VulkanInstancePtr instance, Params params, Caps caps)
    : device_(device),
      physical_device_(physical_device),
      main_queue_(main_queue),
      main_queue_family_(main_queue_family),
      transfer_queue_(transfer_queue),
      transfer_queue_family_(transfer_queue_family),
      instance_(std::move(instance)),
      params_(std::move(params)),
      caps_(std::move(caps)),
      proc_addrs_(PopulateProcAddrs(device_, params_)) {
  dispatch_loader_.init(instance_->vk_instance(), device_);
}

VulkanDeviceQueues::~VulkanDeviceQueues() { device_.destroy(); }

bool VulkanDeviceQueues::ValidateExtensions(vk::PhysicalDevice device,
                                            const std::set<std::string>& required_extension_names,
                                            const std::set<std::string>& required_layer_names) {
  auto extensions = ESCHER_CHECKED_VK_RESULT(device.enumerateDeviceExtensionProperties());

  for (auto& name : required_extension_names) {
    if (!ValidateExtension(device, name, extensions, required_layer_names)) {
      FX_LOGS(WARNING) << "Vulkan has no device extension named: " << name;
      return false;
    }
  }
  return true;
}

VulkanContext VulkanDeviceQueues::GetVulkanContext() const {
  return escher::VulkanContext(instance_->vk_instance(), vk_physical_device(), vk_device(),
                               dispatch_loader(), vk_main_queue(), vk_main_queue_family(),
                               vk_transfer_queue(), vk_transfer_queue_family());
}

}  // namespace escher
