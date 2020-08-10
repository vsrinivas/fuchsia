// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "vulkan_layer.h"

#include <cstring>
#include <iostream>
#include <vector>

#include "utils.h"

namespace {

#ifdef __Fuchsia__
static const char *s_instance_layer_name = "VK_LAYER_FUCHSIA_imagepipe_swapchain_fb";
#else
static const char *s_instance_layer_name = nullptr;
#endif

static const char *s_instance_validation_layer_name = "VK_LAYER_KHRONOS_validation";

static VKAPI_ATTR VkBool32 VKAPI_CALL DebugCallback(
    VkDebugUtilsMessageSeverityFlagBitsEXT msg_severity, VkDebugUtilsMessageTypeFlagsEXT msg_type,
    const VkDebugUtilsMessengerCallbackDataEXT *callback_data, void *user_data) {
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

VulkanLayer::VulkanLayer(std::shared_ptr<VulkanInstance> instance)
    : initialized_(false), instance_(instance) {}

bool VulkanLayer::Init() {
  if (initialized_) {
    RTN_MSG(false, "VulkanLayer is already initialized.\n");
  }

  const auto &instance = *instance_->instance();
  dispatch_loader_ = vk::DispatchLoaderDynamic();
  dispatch_loader_.init(instance, vkGetInstanceProcAddr);

  vk::DebugUtilsMessengerCreateInfoEXT info;
  info.messageSeverity = vk::DebugUtilsMessageSeverityFlagBitsEXT::eVerbose |
                         vk::DebugUtilsMessageSeverityFlagBitsEXT::eWarning |
                         vk::DebugUtilsMessageSeverityFlagBitsEXT::eError;
#if VERBOSE_LOGGING
  info.messageSeverity |= vk::DebugUtilsMessageSeverityFlagBitsEXT::eInfo;
#endif

  info.messageType = vk::DebugUtilsMessageTypeFlagBitsEXT::eGeneral |
                     vk::DebugUtilsMessageTypeFlagBitsEXT::ePerformance |
                     vk::DebugUtilsMessageTypeFlagBitsEXT::eValidation;
  info.pfnUserCallback = DebugCallback;

  auto rv = instance.createDebugUtilsMessengerEXTUnique(info, nullptr, dispatch_loader_);
  if (vk::Result::eSuccess != rv.result) {
    RTN_MSG(false, "VK Error: 0x%x - Failed to create debug messenger.", rv.result);
  }
  debug_messenger_ = std::move(rv.value);
  initialized_ = true;
  return true;
}

void VulkanLayer::AppendRequiredInstanceExtensions(std::vector<const char *> *extensions) {
  extensions->emplace_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
}

void VulkanLayer::AppendRequiredInstanceLayers(std::vector<const char *> *layers) {
  layers->emplace_back(s_instance_layer_name);
}

void VulkanLayer::AppendValidationInstanceLayers(std::vector<const char *> *layers) {
  layers->emplace_back(s_instance_validation_layer_name);
}

void VulkanLayer::AppendRequiredDeviceLayers(std::vector<const char *> *layers) {
  fprintf(stderr, "No required device layers.\n");
}

bool VulkanLayer::CheckValidationLayerSupport() {
  const std::vector<const char *> validation_layers(1, s_instance_validation_layer_name);
  return !FindMatchingProperties(validation_layers, vkp::INSTANCE_LAYER_PROP, nullptr /* device */,
                                 nullptr /* layer */, nullptr /* missing_props */);
}
