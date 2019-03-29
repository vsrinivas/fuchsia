// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lib/escher/vk/vulkan_instance.h"

#include <set>

#include "lib/escher/impl/vulkan_utils.h"
#include "src/lib/fxl/logging.h"

namespace escher {

template <typename FuncT>
static FuncT GetInstanceProcAddr(vk::Instance inst, const char* func_name) {
  FuncT func = reinterpret_cast<FuncT>(inst.getProcAddr(func_name));
  FXL_CHECK(func) << "Could not find Vulkan Instance ProcAddr: " << func_name;
  return func;
}

#define GET_INSTANCE_PROC_ADDR(XXX) \
  XXX = GetInstanceProcAddr<PFN_vk##XXX>(instance, "vk" #XXX)

VulkanInstance::ProcAddrs::ProcAddrs(vk::Instance instance,
                                     bool requires_surface) {
  GET_INSTANCE_PROC_ADDR(CreateDebugReportCallbackEXT);
  GET_INSTANCE_PROC_ADDR(DestroyDebugReportCallbackEXT);
  if (requires_surface) {
    GET_INSTANCE_PROC_ADDR(GetPhysicalDeviceSurfaceSupportKHR);
  }
}

fxl::RefPtr<VulkanInstance> VulkanInstance::New(Params params) {
  params.extension_names.insert(
      VK_KHR_GET_PHYSICAL_DEVICE_PROPERTIES_2_EXTENSION_NAME);
#ifdef __Fuchsia__
  // TODO(ES-143): It's quite possible that this would work on Linux if we
  // uploaded a new Vulkan SDK to the cloud, but there are obstacles to doing
  // this immediately, hence this workaround.  Or, it may be the NVIDIA Vulkan
  // driver itself.
  params.extension_names.insert(
      VK_KHR_EXTERNAL_SEMAPHORE_CAPABILITIES_EXTENSION_NAME);
#endif
  FXL_DCHECK(ValidateLayers(params.layer_names));
  FXL_DCHECK(ValidateExtensions(params.extension_names, params.layer_names));

  // Gather names of layers/extensions to populate InstanceCreateInfo.
  std::vector<const char *> layer_names;
  for (auto &layer : params.layer_names) {
    layer_names.push_back(layer.c_str());
  }
  std::vector<const char *> extension_names;
  for (auto &extension : params.extension_names) {
    extension_names.push_back(extension.c_str());
  }

  vk::InstanceCreateInfo info;
  info.enabledLayerCount = layer_names.size();
  info.ppEnabledLayerNames = layer_names.data();
  info.enabledExtensionCount = extension_names.size();
  info.ppEnabledExtensionNames = extension_names.data();

  auto result = vk::createInstance(info);
  if (result.result != vk::Result::eSuccess) {
    FXL_LOG(WARNING) << "Could not create Vulkan Instance.";
    return fxl::RefPtr<VulkanInstance>();
  }

  return fxl::AdoptRef(new VulkanInstance(result.value, std::move(params)));
}

VulkanInstance::VulkanInstance(vk::Instance instance, Params params)
    : instance_(instance),
      params_(std::move(params)),
      proc_addrs_(instance_, params_.requires_surface) {}

VulkanInstance::~VulkanInstance() { instance_.destroy(); }

bool VulkanInstance::ValidateLayers(
    const std::set<std::string> &required_layer_names) {
  auto properties =
      ESCHER_CHECKED_VK_RESULT(vk::enumerateInstanceLayerProperties());

  for (auto &name : required_layer_names) {
    auto found = std::find_if(properties.begin(), properties.end(),
                              [&name](vk::LayerProperties &layer) {
                                return !strncmp(layer.layerName, name.c_str(),
                                                VK_MAX_EXTENSION_NAME_SIZE);
                              });
    if (found == properties.end()) {
      FXL_LOG(WARNING) << "Vulkan has no instance layer named: " << name;
      return false;
    }
  }
  return true;
}

// Helper for ValidateExtensions().
static bool ValidateExtension(
    const std::string name,
    const std::vector<vk::ExtensionProperties> &base_extensions,
    const std::set<std::string> &required_layer_names) {
  auto found =
      std::find_if(base_extensions.begin(), base_extensions.end(),
                   [&name](const vk::ExtensionProperties &extension) {
                     return !strncmp(extension.extensionName, name.c_str(),
                                     VK_MAX_EXTENSION_NAME_SIZE);
                   });
  if (found != base_extensions.end())
    return true;

  // Didn't find the extension in the base list of extensions.  Perhaps it is
  // implemented in a layer.
  for (auto &layer_name : required_layer_names) {
    auto layer_extensions = ESCHER_CHECKED_VK_RESULT(
        vk::enumerateInstanceExtensionProperties(layer_name));
    FXL_LOG(INFO) << "Looking for Vulkan instance extension: " << name
                  << " in layer: " << layer_name;

    auto found =
        std::find_if(layer_extensions.begin(), layer_extensions.end(),
                     [&name](vk::ExtensionProperties &extension) {
                       return !strncmp(extension.extensionName, name.c_str(),
                                       VK_MAX_EXTENSION_NAME_SIZE);
                     });
    if (found != layer_extensions.end())
      return true;
  }

  return false;
}

bool VulkanInstance::ValidateExtensions(
    const std::set<std::string> &required_extension_names,
    const std::set<std::string> &required_layer_names) {
  auto extensions =
      ESCHER_CHECKED_VK_RESULT(vk::enumerateInstanceExtensionProperties());

  for (auto &name : required_extension_names) {
    if (!ValidateExtension(name, extensions, required_layer_names)) {
      FXL_LOG(WARNING) << "Vulkan has no instance extension named: " << name;
      return false;
    }
  }
  return true;
}

};  // namespace escher
