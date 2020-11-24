// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/graphics/examples/vkprimer/common/instance.h"

#include <iostream>
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
std::vector<const char *> GetExtensionsPrivate(bool enable_validation) {
  std::vector<const char *> required_props = s_required_props;
  std::vector<const char *> extensions;
  std::vector<std::string> missing_props;

#ifdef __Fuchsia__
  const char *kMagmaLayer = "VK_LAYER_FUCHSIA_imagepipe_swapchain_fb";
#else
  const char *kMagmaLayer = nullptr;
#endif

  if (enable_validation) {
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

Instance::~Instance() { initialized_ = false; }

#if USE_GLFW
bool Instance::Init(bool enable_validation, GLFWwindow *window) {
  window_ = window;
#else
bool Instance::Init() {
#endif
  RTN_IF_MSG(false, (initialized_ == true), "Already initialized.\n");

  // Application Info
  vk::ApplicationInfo app_info;
  const uint32_t kMajor = 1;
  const uint32_t kMinor = 1;
  app_info.apiVersion = VK_MAKE_VERSION(kMajor, kMinor, 0);
  app_info.pApplicationName = "VkPrimer";
  fprintf(stdout, "\nVulkan Instance API Version: %d.%d\n\n", kMajor, kMinor);

  // Instance Create Info
  vk::InstanceCreateInfo instance_info;
  instance_info.pApplicationInfo = &app_info;

  // Extensions
  extensions_ = GetExtensions();

  instance_info.enabledExtensionCount = static_cast<uint32_t>(extensions_.size());
  instance_info.ppEnabledExtensionNames = extensions_.data();

// Layers
#ifdef __Fuchsia__
  layers_.emplace_back("VK_LAYER_FUCHSIA_imagepipe_swapchain_fb");
#endif

  if (enable_validation_) {
    layers_.emplace_back("VK_LAYER_KHRONOS_validation");
  }

  instance_info.enabledLayerCount = static_cast<uint32_t>(layers_.size());
  instance_info.ppEnabledLayerNames = layers_.data();
  PrintProps(extensions_, "Enabled Instance Extensions");
  PrintProps(layers_, "Enabled Layers");

  auto [r_instance, instance] = vk::createInstanceUnique(instance_info);
  RTN_IF_VKH_ERR(false, r_instance, "Failed to create instance\n");
  instance_ = std::move(instance);
  initialized_ = true;
  return true;
}

std::vector<const char *> Instance::GetExtensions() {
#if USE_GLFW
  extensions_ = GetExtensionsGLFW(enable_validation_);
#else
  extensions_ = GetExtensionsPrivate(enable_validation_);
#endif

  return extensions_;
}

bool Instance::ConfigureDebugMessenger() {
  dispatch_loader_ = vk::DispatchLoaderDynamic();
  dispatch_loader_.init(instance_.get(), vkGetInstanceProcAddr);

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
      instance_->createDebugUtilsMessengerEXTUnique(info, nullptr, dispatch_loader_);
  RTN_IF_VKH_ERR(false, r_messenger, "Failed to create debug messenger.\n");
  debug_messenger_ = std::move(messenger);
  initialized_ = true;
  return true;
}

const vk::Instance &Instance::get() const { return instance_.get(); }

}  // namespace vkp
