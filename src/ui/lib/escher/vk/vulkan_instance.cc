// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/lib/escher/vk/vulkan_instance.h"

#include <lib/syslog/cpp/macros.h>

#include <set>

#include "src/ui/lib/escher/impl/vulkan_utils.h"

namespace escher {

template <typename FuncT>
static FuncT GetInstanceProcAddr(vk::Instance inst, const char *func_name) {
  FuncT func = reinterpret_cast<FuncT>(inst.getProcAddr(func_name));
  FX_CHECK(func) << "Could not find Vulkan Instance ProcAddr: " << func_name;
  return func;
}

#define GET_INSTANCE_PROC_ADDR(XXX) vk##XXX = GetInstanceProcAddr<PFN_vk##XXX>(instance, "vk" #XXX)

VulkanInstance::ProcAddrs::ProcAddrs(vk::Instance instance, bool requires_surface) {
  GET_INSTANCE_PROC_ADDR(CreateDebugReportCallbackEXT);
  GET_INSTANCE_PROC_ADDR(DestroyDebugReportCallbackEXT);
  if (requires_surface) {
    GET_INSTANCE_PROC_ADDR(GetPhysicalDeviceSurfaceSupportKHR);
  }
}

fxl::RefPtr<VulkanInstance> VulkanInstance::New(Params params) {
  params.extension_names.insert(VK_KHR_GET_PHYSICAL_DEVICE_PROPERTIES_2_EXTENSION_NAME);
#ifdef __Fuchsia__
  // TODO(fxbug.dev/7234): It's quite possible that this would work on Linux if we
  // uploaded a new Vulkan SDK to the cloud, but there are obstacles to doing
  // this immediately, hence this workaround.  Or, it may be the NVIDIA Vulkan
  // driver itself.
  params.extension_names.insert(VK_KHR_EXTERNAL_SEMAPHORE_CAPABILITIES_EXTENSION_NAME);
#endif
  FX_DCHECK(ValidateLayers(params.layer_names));
  FX_DCHECK(ValidateExtensions(params.extension_names, params.layer_names));

  // Gather names of layers/extensions to populate InstanceCreateInfo.
  std::vector<const char *> layer_names;
  for (auto &layer : params.layer_names) {
    layer_names.push_back(layer.c_str());
  }
  std::vector<const char *> extension_names;
  for (auto &extension : params.extension_names) {
    extension_names.push_back(extension.c_str());
  }

  uint32_t api_version = 0;
  if (vkEnumerateInstanceVersion(&api_version) != VK_SUCCESS) {
    api_version = VK_API_VERSION_1_0;
  }

  vk::ApplicationInfo app_info;
  app_info.pApplicationName = "Escher";
  app_info.apiVersion = api_version;

  vk::InstanceCreateInfo info;
  info.pApplicationInfo = &app_info;
  info.enabledLayerCount = static_cast<uint32_t>(layer_names.size());
  info.ppEnabledLayerNames = layer_names.data();
  info.enabledExtensionCount = static_cast<uint32_t>(extension_names.size());
  info.ppEnabledExtensionNames = extension_names.data();

  auto result = vk::createInstance(info);
  if (result.result != vk::Result::eSuccess) {
    FX_LOGS(WARNING) << "Could not create Vulkan Instance: " << vk::to_string(result.result) << ".";
    return fxl::RefPtr<VulkanInstance>();
  }

  return fxl::AdoptRef(new VulkanInstance(result.value, std::move(params), api_version));
}

VulkanInstance::VulkanInstance(vk::Instance instance, Params params, uint32_t api_version)
    : instance_(instance),
      params_(std::move(params)),
      proc_addrs_(instance_, params_.requires_surface),
      api_version_(api_version) {
  // Register global debug report callback function.
  // Do this only if extension VK_EXT_debug_report is enabled.
  if (HasDebugReportExt()) {
    auto callback_flags = vk::DebugReportFlagBitsEXT::eError |
                          vk::DebugReportFlagBitsEXT::eWarning |
                          vk::DebugReportFlagBitsEXT::ePerformanceWarning;
    vk::DebugReportCallbackCreateInfoEXT create_info(callback_flags, DebugReportCallbackEntrance,
                                                     this);
    auto create_callback_result =
        vk_instance().createDebugReportCallbackEXT(create_info, nullptr, proc_addrs());
    FX_CHECK(create_callback_result.result == vk::Result::eSuccess);
    vk_callback_entrance_handle_ = create_callback_result.value;
  }
}

VulkanInstance::~VulkanInstance() {
  // Unregister global debug report callback function.
  // Do this only if extension VK_EXT_debug_report is enabled.
  if (HasDebugReportExt()) {
    vk_instance().destroyDebugReportCallbackEXT(vk_callback_entrance_handle_, nullptr,
                                                proc_addrs());
  }
  instance_.destroy();
}

std::optional<std::string> VulkanInstance::GetValidationLayerName() {
  const std::string kDeprecatedLayerName = "VK_LAYER_LUNARG_standard_validation";
  const std::string kNewLayerName = "VK_LAYER_KHRONOS_validation";

  return ValidateLayers({kNewLayerName})          ? std::make_optional(kNewLayerName)
         : ValidateLayers({kDeprecatedLayerName}) ? std::make_optional(kDeprecatedLayerName)
                                                  : std::nullopt;
}

bool VulkanInstance::ValidateLayers(const std::set<std::string> &required_layer_names) {
  auto properties = ESCHER_CHECKED_VK_RESULT(vk::enumerateInstanceLayerProperties());

  for (auto &name : required_layer_names) {
    auto found =
        std::find_if(properties.begin(), properties.end(), [&name](vk::LayerProperties &layer) {
          return !strncmp(layer.layerName, name.c_str(), VK_MAX_EXTENSION_NAME_SIZE);
        });
    if (found == properties.end()) {
      FX_LOGS(WARNING) << "Vulkan has no instance layer named: " << name;
      return false;
    }
  }
  return true;
}

// Helper for ValidateExtensions().
static bool ValidateExtension(const std::string name,
                              const std::vector<vk::ExtensionProperties> &base_extensions,
                              const std::set<std::string> &required_layer_names) {
  auto found = std::find_if(base_extensions.begin(), base_extensions.end(),
                            [&name](const vk::ExtensionProperties &extension) {
                              return !strncmp(extension.extensionName, name.c_str(),
                                              VK_MAX_EXTENSION_NAME_SIZE);
                            });
  if (found != base_extensions.end())
    return true;

  // Didn't find the extension in the base list of extensions.  Perhaps it is
  // implemented in a layer.
  for (auto &layer_name : required_layer_names) {
    auto layer_extensions =
        ESCHER_CHECKED_VK_RESULT(vk::enumerateInstanceExtensionProperties(layer_name));
    FX_LOGS(INFO) << "Looking for Vulkan instance extension: " << name
                  << " in layer: " << layer_name;

    auto found = std::find_if(layer_extensions.begin(), layer_extensions.end(),
                              [&name](vk::ExtensionProperties &extension) {
                                return !strncmp(extension.extensionName, name.c_str(),
                                                VK_MAX_EXTENSION_NAME_SIZE);
                              });
    if (found != layer_extensions.end())
      return true;
  }

  return false;
}

bool VulkanInstance::ValidateExtensions(const std::set<std::string> &required_extension_names,
                                        const std::set<std::string> &required_layer_names) {
  auto extensions = ESCHER_CHECKED_VK_RESULT(vk::enumerateInstanceExtensionProperties());

  for (auto &name : required_extension_names) {
    if (!ValidateExtension(name, extensions, required_layer_names)) {
      FX_LOGS(WARNING) << "Vulkan has no instance extension named: " << name;
      return false;
    }
  }
  return true;
}

VulkanInstance::DebugReportCallbackHandle VulkanInstance::RegisterDebugReportCallback(
    VkDebugReportCallbackFn function, void *user_data) {
  callbacks_.emplace_back(std::move(function), user_data);
  return std::prev(callbacks_.end());
}

void VulkanInstance::DeregisterDebugReportCallback(
    const VulkanInstance::DebugReportCallbackHandle &handle) {
  callbacks_.erase(handle);
}

VkBool32 VulkanInstance::DebugReportCallbackEntrance(VkDebugReportFlagsEXT flags,
                                                     VkDebugReportObjectTypeEXT objectType,
                                                     uint64_t object, size_t location,
                                                     int32_t messageCode, const char *pLayerPrefix,
                                                     const char *pMessage, void *pUserData) {
  auto *instance_ptr = reinterpret_cast<VulkanInstance *>(pUserData);
  for (const auto &callback : instance_ptr->callbacks_) {
    callback.function(flags, objectType, object, location, messageCode, pLayerPrefix, pMessage,
                      callback.user_data);
  }
  return VK_FALSE;
}

};  // namespace escher
