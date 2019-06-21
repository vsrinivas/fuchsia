// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "vulkan_layer.h"

#include <cstring>
#include <iostream>
#include <vector>

#include "utils.h"

namespace {

static const std::vector<const char *> s_instance_layer_names = {
#ifdef __Fuchsia__
    "VK_LAYER_FUCHSIA_imagepipe_swapchain_fb",
#endif
    "VK_LAYER_LUNARG_standard_validation",
};

static VKAPI_ATTR VkBool32 VKAPI_CALL
VulkanDebugCallback(VkDebugUtilsMessageSeverityFlagBitsEXT msg_severity,
                    VkDebugUtilsMessageTypeFlagsEXT msg_type,
                    const VkDebugUtilsMessengerCallbackDataEXT *callback_data,
                    void *user_data) {
  std::cerr << "VKCB Layer Layer: " << callback_data->pMessage << std::endl;

  if (msg_type & VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT) {
    std::cout << "VKCB Type General" << std::endl;
  }
  if (msg_type & VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT) {
    std::cout << "VKCB Type Layer" << std::endl;
  }
  if (msg_type & VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT) {
    std::cout << "VKCB Type Performance" << std::endl;
  }
  if (msg_severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT) {
    std::cout << "VKCB Severity Verbose" << std::endl;
  }
  if (msg_severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT) {
    std::cout << "VKCB Severity Info" << std::endl;
  }
  if (msg_severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT) {
    std::cout << "VKCB Severity Warning" << std::endl;
  }
  if (msg_severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT) {
    std::cout << "VKCB Severity Error" << std::endl;
  }
  return VK_FALSE;
}

}  // namespace

VkResult CreateVulkanDebugUtilsMessenger(
    const VkInstance &instance,
    const VkDebugUtilsMessengerCreateInfoEXT *create_info,
    const VkAllocationCallbacks *allocator,
    VkDebugUtilsMessengerEXT *callback) {
  auto func = (PFN_vkCreateDebugUtilsMessengerEXT)vkGetInstanceProcAddr(
      instance, "vkCreateDebugUtilsMessengerEXT");
  if (func != nullptr) {
    return func(instance, create_info, allocator, callback);
  } else {
    return VK_ERROR_EXTENSION_NOT_PRESENT;
  }
}

bool DestroyVulkanDebugUtilsMessenger(const VkInstance &instance,
                                      const VkDebugUtilsMessengerEXT &callback,
                                      const VkAllocationCallbacks *allocator) {
  auto func = (PFN_vkDestroyDebugUtilsMessengerEXT)vkGetInstanceProcAddr(
      instance, "vkDestroyDebugUtilsMessengerEXT");
  if (func != nullptr) {
    func(instance, callback, allocator);
  } else {
    RTN_MSG(false, "Unable to destroy debug messenger.\n");
  }
  return true;
}

VulkanLayer::VulkanLayer(std::shared_ptr<VulkanInstance> instance)
    : initialized_(false), instance_(instance) {}

bool VulkanLayer::Init() {
  if (initialized_) {
    RTN_MSG(false, "VulkanLayer is already initialized.\n");
  }

  SetupDebugCallback();
  initialized_ = true;
  return true;
}

VulkanLayer::~VulkanLayer() {
  if (initialized_) {
    DestroyVulkanDebugUtilsMessenger(instance_->instance(), callback_, nullptr);
    initialized_ = false;
  }
}

void VulkanLayer::AppendRequiredInstanceExtensions(
    std::vector<const char *> *extensions) {
  extensions->emplace_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
}

void VulkanLayer::AppendRequiredInstanceLayers(
    std::vector<const char *> *layers) {
  for (const auto &layer : s_instance_layer_names) {
    layers->emplace_back(layer);
  }
}

void VulkanLayer::AppendRequiredDeviceLayers(
    std::vector<const char *> *layers) {
  fprintf(stderr, "No required device layers.\n");
}

bool VulkanLayer::CheckInstanceLayerSupport() {
  return !FindMatchingProperties(s_instance_layer_names, INSTANCE_LAYER_PROP,
                                 nullptr /* device */, nullptr /* layer */,
                                 nullptr /* missing_props */);
}

bool VulkanLayer::SetupDebugCallback() {
  VkDebugUtilsMessengerCreateInfoEXT create_info = {
      .sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT,
      .messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT |
                         VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
                         VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT,
      .messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
                     VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
                     VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT,
      .pfnUserCallback = VulkanDebugCallback,
  };

  if (CreateVulkanDebugUtilsMessenger(instance_->instance(), &create_info,
                                      nullptr, &callback_) != VK_SUCCESS) {
    RTN_MSG(false, "Failed to set up debug callback.\n");
  }
  return true;
}
