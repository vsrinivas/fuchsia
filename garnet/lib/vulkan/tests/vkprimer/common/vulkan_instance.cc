// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "vulkan_instance.h"

#include <vector>

#include "utils.h"
#include "vulkan_layer.h"

namespace {

static const std::vector<const char *> s_required_props = {
    VK_KHR_EXTERNAL_MEMORY_CAPABILITIES_EXTENSION_NAME,
    VK_KHR_EXTERNAL_SEMAPHORE_CAPABILITIES_EXTENSION_NAME,
    VK_KHR_SURFACE_EXTENSION_NAME,
    VK_KHR_GET_PHYSICAL_DEVICE_PROPERTIES_2_EXTENSION_NAME,
#ifdef __Fuchsia__
    VK_FUCHSIA_IMAGEPIPE_SURFACE_EXTENSION_NAME,
#endif
};

static const std::vector<const char *> s_desired_props = {
    VK_EXT_DEBUG_REPORT_EXTENSION_NAME,
};

static void PrintProps(const std::vector<const char *> &props) {
  for (const auto &prop : props) {
    printf("\t%s\n", prop);
  }
  printf("\n");
}

#if USE_GLFW
std::vector<const char *> GetExtensionsGLFW() {
  uint32_t num_extensions = 0;
  const char **glfw_extensions;
  glfw_extensions = glfwGetRequiredInstanceExtensions(&num_extensions);
  std::vector<const char *> extensions(glfw_extensions,
                                       glfw_extensions + num_extensions);
  extensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);

  return extensions;
}
#else
std::vector<const char *> GetExtensions_Private(
    std::vector<const char *> *layers) {
  const char *kMagmaLayer = "VK_LAYER_FUCHSIA_imagepipe_swapchain_fb";

  std::vector<const char *> extensions;
  std::vector<std::string> missing_props;

  if (!FindMatchingProperties(s_required_props, INSTANCE_EXT_PROP,
                              nullptr /* device */, kMagmaLayer,
                              &missing_props)) {
    extensions.insert(extensions.end(), s_required_props.begin(),
                      s_required_props.end());
  }

  return extensions;
}
#endif

}  // namespace

VulkanInstance::~VulkanInstance() {
  if (initialized_) {
    vkDestroyInstance(instance_, nullptr);
    initialized_ = false;
  }
}

#if USE_GLFW
bool VulkanInstance::Init(bool enable_validation, GLFWwindow *window) {
  window_ = window;
#else
bool VulkanInstance::Init(bool enable_validation) {
#endif
  if (enable_validation && !VulkanLayer::CheckInstanceLayerSupport()) {
    RTN_MSG(false, "Validation layers requested, but not available!");
  }

  // Application Info
  VkApplicationInfo app_info = {
      .sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
      .apiVersion = VK_API_VERSION_1_0,
      .pApplicationName = "VkPrimer",
      .applicationVersion = VK_MAKE_VERSION(1, 0, 0),
      .pEngineName = "No Engine",
      .engineVersion = VK_MAKE_VERSION(1, 0, 0),
  };

  // Instance Create Info
  VkInstanceCreateInfo instance_create_info = {
      .sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
      .pApplicationInfo = &app_info,
  };

  // Extensions
  extensions_ = GetExtensions();

  // Layers
  if (enable_validation) {
    VulkanLayer::AppendRequiredInstanceExtensions(&extensions_);
    VulkanLayer::AppendRequiredInstanceLayers(&layers_);
  } else {
    instance_create_info.enabledLayerCount = 0;
  }

  instance_create_info.enabledExtensionCount =
      static_cast<uint32_t>(extensions_.size());
  instance_create_info.ppEnabledExtensionNames = extensions_.data();

  instance_create_info.enabledLayerCount =
      static_cast<uint32_t>(layers_.size());
  instance_create_info.ppEnabledLayerNames = layers_.data();

  fprintf(stderr, "Enabled Instance Extensions:\n");
  PrintProps(extensions_);

  fprintf(stderr, "Enabled layers:\n");
  for (auto &layer : layers_) {
    fprintf(stderr, "\t%s\n", layer);
  }
  fprintf(stderr, "\n");

  auto err = vkCreateInstance(&instance_create_info, nullptr, &instance_);
  if (VK_SUCCESS != err) {
    RTN_MSG(false, "VK Error: 0x%x - Failed to create instance.", err);
  }

  initialized_ = true;
  return true;
}

std::vector<const char *> VulkanInstance::GetExtensions() {
#if USE_GLFW
  extensions_ = GetExtensionsGLFW();
#else
  extensions_ = GetExtensions_Private(&layers_);
#endif

  return extensions_;
}
