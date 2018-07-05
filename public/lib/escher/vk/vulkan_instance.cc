// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lib/escher/vk/vulkan_instance.h"

#include <set>

#include "lib/escher/impl/vulkan_utils.h"
#include "lib/fxl/logging.h"

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
  FXL_DCHECK(ValidateLayers(params.layer_names));
  FXL_DCHECK(ValidateExtensions(params.extension_names));

  // Gather names of layers/extensions to populate InstanceCreateInfo.
  std::vector<const char*> layer_names;
  for (auto& layer : params.layer_names) {
    layer_names.push_back(layer.c_str());
  }
  std::vector<const char*> extension_names;
  for (auto& extension : params.extension_names) {
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
    const std::set<std::string>& required_layer_names) {
  auto properties =
      ESCHER_CHECKED_VK_RESULT(vk::enumerateInstanceLayerProperties());

  for (auto& name : required_layer_names) {
    auto found = std::find_if(properties.begin(), properties.end(),
                              [&name](vk::LayerProperties& layer) {
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

bool VulkanInstance::ValidateExtensions(
    const std::set<std::string>& required_extension_names) {
  auto extensions =
      ESCHER_CHECKED_VK_RESULT(vk::enumerateInstanceExtensionProperties());

  for (auto& name : required_extension_names) {
    auto found =
        std::find_if(extensions.begin(), extensions.end(),
                     [&name](vk::ExtensionProperties& extension) {
                       return !strncmp(extension.extensionName, name.c_str(),
                                       VK_MAX_EXTENSION_NAME_SIZE);
                     });
    if (found == extensions.end()) {
      FXL_LOG(WARNING) << "Vulkan has no instance extension named: " << name;
      return false;
    }
  }
  return true;
}

};  // namespace escher
