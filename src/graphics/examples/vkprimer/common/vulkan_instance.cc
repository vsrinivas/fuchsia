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
  std::vector<const char *> extensions(glfw_extensions, glfw_extensions + num_extensions);
  extensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);

  return extensions;
}
#else
std::vector<const char *> GetExtensions_Private(std::vector<const char *> *layers) {
  const char *kMagmaLayer = "VK_LAYER_FUCHSIA_imagepipe_swapchain_fb";

  std::vector<const char *> extensions;
  std::vector<std::string> missing_props;

  if (!FindMatchingProperties(s_required_props, vkp::INSTANCE_EXT_PROP, nullptr /* phys_device */,
                              kMagmaLayer, &missing_props)) {
    extensions.insert(extensions.end(), s_required_props.begin(), s_required_props.end());
  }

  return extensions;
}
#endif

}  // namespace

VulkanInstance::~VulkanInstance() { initialized_ = false; }

#if USE_GLFW
bool VulkanInstance::Init(bool enable_validation, GLFWwindow *window) {
  window_ = window;
#else
bool VulkanInstance::Init(bool enable_validation) {
#endif
  if (enable_validation && !VulkanLayer::CheckValidationLayerSupport()) {
    RTN_MSG(false, "Validation layers requested, but not available!");
  }

  // Application Info
  vk::ApplicationInfo app_info;
  app_info.apiVersion = VK_MAKE_VERSION(1, 0, 0);
  app_info.pApplicationName = "VkPrimer";
  app_info.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
  app_info.pEngineName = "No Engine";
  app_info.engineVersion = VK_MAKE_VERSION(1, 0, 0);

  // Instance Create Info
  vk::InstanceCreateInfo create_info;
  create_info.pApplicationInfo = &app_info;

  // Extensions
  extensions_ = GetExtensions();
  VulkanLayer::AppendRequiredInstanceExtensions(&extensions_);

  create_info.enabledExtensionCount = static_cast<uint32_t>(extensions_.size());
  create_info.ppEnabledExtensionNames = extensions_.data();

  // Layers
  VulkanLayer::AppendRequiredInstanceLayers(&layers_);

  if (enable_validation) {
    VulkanLayer::AppendValidationInstanceLayers(&layers_);
  }

  create_info.enabledLayerCount = static_cast<uint32_t>(layers_.size());
  create_info.ppEnabledLayerNames = layers_.data();

  fprintf(stderr, "Enabled Instance Extensions:\n");
  PrintProps(extensions_);

  fprintf(stderr, "Enabled layers:\n");
  for (auto &layer : layers_) {
    fprintf(stderr, "\t%s\n", layer);
  }
  fprintf(stderr, "\n");

  auto rv = vk::createInstanceUnique(create_info);
  if (vk::Result::eSuccess != rv.result) {
    RTN_MSG(false, "VK Error: 0x%x - Failed to create instance.", rv.result);
  }
  instance_ = std::move(rv.value);

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

const vk::UniqueInstance &VulkanInstance::instance() const { return instance_; }
