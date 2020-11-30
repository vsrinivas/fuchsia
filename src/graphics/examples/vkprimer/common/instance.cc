// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/graphics/examples/vkprimer/common/instance.h"

#include <iostream>
#include <memory>
#include <vector>

#include "src/graphics/examples/vkprimer/common/utils.h"

namespace {

const std::vector<const char *> s_required_props = {
    VK_KHR_EXTERNAL_MEMORY_CAPABILITIES_EXTENSION_NAME,
    VK_KHR_EXTERNAL_SEMAPHORE_CAPABILITIES_EXTENSION_NAME,
    VK_KHR_SURFACE_EXTENSION_NAME,
#ifdef __Fuchsia__
    VK_FUCHSIA_IMAGEPIPE_SURFACE_EXTENSION_NAME,
#endif
};

void PrintProps(const std::vector<const char *> &props, const char *msg) {
  printf("%s\n", msg);
  for (const auto &prop : props) {
    printf("\t%s\n", prop);
  }
  printf("\n");
}

#if USE_GLFW
std::vector<const char *> GetExtensionsGLFW(bool enable_validation) {
  uint32_t num_extensions = 0;
  const char **glfw_extensions;
  glfw_extensions = glfwGetRequiredInstanceExtensions(&num_extensions);
  std::vector<const char *> extensions(glfw_extensions, glfw_extensions + num_extensions);
  if (enable_validation) {
    extensions.emplace_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
  }

  return extensions;
}
#else
std::vector<const char *> GetExtensionsPrivate(bool validation_layers_enabled) {
  std::vector<const char *> required_props = s_required_props;
  std::vector<const char *> extensions;
  std::vector<std::string> missing_props;

#ifdef __Fuchsia__
  const char *kMagmaLayer = "VK_LAYER_FUCHSIA_imagepipe_swapchain_fb";
#else
  const char *kMagmaLayer = nullptr;
#endif

  if (validation_layers_enabled) {
    required_props.emplace_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
  }

  if (FindRequiredProperties(s_required_props, vkp::INSTANCE_EXT_PROP, nullptr /* phys_device */,
                             kMagmaLayer, &missing_props)) {
    extensions.insert(extensions.end(), s_required_props.begin(), s_required_props.end());
  }

  return extensions;
}
#endif

VKAPI_ATTR VkBool32 VKAPI_CALL
DebugCallback(VkDebugUtilsMessageSeverityFlagBitsEXT severity, VkDebugUtilsMessageTypeFlagsEXT type,
              const VkDebugUtilsMessengerCallbackDataEXT *callback_data, void *user_data) {
  std::string severity_str{};
  if (severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT) {
    severity_str = "VERBOSE";
  } else if (severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT) {
    severity_str = "INFO";
  } else if (severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT) {
    severity_str = "WARNING";
  } else if (severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT) {
    severity_str = "ERROR";
  }

  std::string type_str{};
  if (type & VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT) {
    type_str = "General";
  } else if (type & VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT) {
    type_str = "Validation";
  } else if (type & VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT) {
    type_str = "Performance";
  } else {
    type_str = "Unknown";
  }

  if (severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT) {
    std::cerr << "VK[" << severity_str << "]\tType: " << type_str << "\tMessage:\n\t"
              << callback_data->pMessage << std::endl
              << std::endl;
  } else {
    std::cout << "VK[" << severity_str << "]\tType: " << type_str << "\tMessage:\n\t"
              << callback_data->pMessage << std::endl
              << std::endl;
  }
  return VK_FALSE;
}

}  // namespace

namespace vkp {

Instance::Instance(const vk::InstanceCreateInfo &instance_info, bool validation_layers_enabled,
                   vk::Optional<const vk::AllocationCallbacks> allocator)
    : instance_info_(instance_info),
      validation_layers_enabled_(validation_layers_enabled),
      allocator_(allocator) {}

Instance::~Instance() {
  if (initialized_) {
    debug_messenger_.reset();
    initialized_ = false;
  }
}

bool Instance::Init() {
  RTN_IF_MSG(false, (initialized_ == true), "Already initialized.\n");

  // Extensions
  extensions_ = GetExtensions();

  // Require api version 1.1 if it hasn't been set.
  vk::ApplicationInfo app_info;
  app_info.apiVersion = VK_MAKE_VERSION(1, 1, 0);
  if (instance_info_.pApplicationInfo == nullptr) {
    instance_info_.pApplicationInfo = &app_info;
  } else if (instance_info_.pApplicationInfo->apiVersion == 0) {
    RTN_MSG(false,
            "Must set vk::ApplicationInfo::apiVersion when customizing vk::ApplicationInfo.\n");
  }

// Layers
#ifdef __Fuchsia__
  layers_.emplace_back("VK_LAYER_FUCHSIA_imagepipe_swapchain_fb");
#endif

  if (validation_layers_enabled_) {
    layers_.emplace_back("VK_LAYER_KHRONOS_validation");
    extensions_.emplace_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
  }

  if (instance_info_.ppEnabledLayerNames && instance_info_.enabledLayerCount) {
    // Tack on custom layers.
    std::copy(instance_info_.ppEnabledLayerNames,
              instance_info_.ppEnabledLayerNames + instance_info_.enabledLayerCount,
              std::back_inserter(layers_));
  }
  instance_info_.enabledLayerCount = static_cast<uint32_t>(layers_.size());
  instance_info_.ppEnabledLayerNames = layers_.data();

  if (instance_info_.ppEnabledExtensionNames && instance_info_.enabledExtensionCount) {
    // Tack on custom extensions.
    std::copy(instance_info_.ppEnabledExtensionNames,
              instance_info_.ppEnabledExtensionNames + instance_info_.enabledExtensionCount,
              std::back_inserter(extensions_));
  }
  instance_info_.enabledExtensionCount = static_cast<uint32_t>(extensions_.size());
  instance_info_.ppEnabledExtensionNames = extensions_.data();

  PrintProps(extensions_, "Enabled Instance Extensions");
  PrintProps(layers_, "Enabled Layers");

  auto [r_instance, instance] = vk::createInstanceUnique(instance_info_);
  RTN_IF_VKH_ERR(false, r_instance, "Failed to create instance\n");
  instance_ = std::move(instance);
  if (validation_layers_enabled_) {
    ConfigureDebugMessenger(instance_.get());
  }
  initialized_ = true;
  return true;
}

std::vector<const char *> Instance::GetExtensions() {
#if USE_GLFW
  extensions_ = GetExtensionsGLFW(validation_layers_enabled_);
#else
  extensions_ = GetExtensionsPrivate(validation_layers_enabled_);
#endif

  return extensions_;
}

bool Instance::ConfigureDebugMessenger(const vk::Instance &instance) {
  dispatch_loader_ = vk::DispatchLoaderDynamic();
  dispatch_loader_.init(instance, vkGetInstanceProcAddr);

  vk::DebugUtilsMessengerCreateInfoEXT info;
  info.messageSeverity = vk::DebugUtilsMessageSeverityFlagBitsEXT::eInfo |
                         vk::DebugUtilsMessageSeverityFlagBitsEXT::eWarning |
                         vk::DebugUtilsMessageSeverityFlagBitsEXT::eError;

#if VERBOSE_LOGGING
  info.messageSeverity |= vk::DebugUtilsMessageSeverityFlagBitsEXT::eVerbose;
#endif

  info.messageType = vk::DebugUtilsMessageTypeFlagBitsEXT::eGeneral |
                     vk::DebugUtilsMessageTypeFlagBitsEXT::ePerformance |
                     vk::DebugUtilsMessageTypeFlagBitsEXT::eValidation;
  info.pfnUserCallback = DebugCallback;

  auto [r_messenger, messenger] =
      instance.createDebugUtilsMessengerEXTUnique(info, nullptr, dispatch_loader_);
  RTN_IF_VKH_ERR(false, r_messenger, "Failed to create debug messenger.\n");
  debug_messenger_ = std::move(messenger);
  return true;
}

const vk::Instance &Instance::get() const { return instance_.get(); }

Instance::Builder::Builder() : allocator_(nullptr) {}

Instance::Builder &Instance::Builder::set_allocator(
    const vk::Optional<const vk::AllocationCallbacks> &v) {
  allocator_ = v;
  return *this;
}

Instance::Builder &Instance::Builder::set_instance_info(const vk::InstanceCreateInfo &v) {
  instance_info_ = v;
  return *this;
}

Instance::Builder &Instance::Builder::set_validation_layers_enabled(bool v) {
  validation_layers_enabled_ = v;
  return *this;
}

std::unique_ptr<Instance> Instance::Builder::Unique() const {
  auto instance =
      std::make_unique<Instance>(instance_info_, validation_layers_enabled_, allocator_);
  if (!instance->Init()) {
    RTN_MSG(nullptr, "Failed to initialize Instance.\n")
  }
  return instance;
}

std::shared_ptr<Instance> Instance::Builder::Shared() const {
  auto instance =
      std::make_shared<Instance>(instance_info_, validation_layers_enabled_, allocator_);
  if (!instance->Init()) {
    RTN_MSG(nullptr, "Failed to initialize Instance.\n")
  }
  return instance;
}

}  // namespace vkp
