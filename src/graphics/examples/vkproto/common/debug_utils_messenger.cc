// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/graphics/examples/vkproto/common/debug_utils_messenger.h"

#include <iostream>
#include <memory>
#include <vector>

#include "src/graphics/examples/vkproto/common/utils.h"

#include <vulkan/vulkan.hpp>

namespace {

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

DebugUtilsMessenger::DebugUtilsMessenger(std::shared_ptr<vk::Instance> instance)
    : initialized_(false), instance_(instance), use_defaults_(true) {}

DebugUtilsMessenger::DebugUtilsMessenger(std::shared_ptr<vk::Instance> instance,
                                         const vk::DebugUtilsMessengerCreateInfoEXT &info)
    : initialized_(false), instance_(instance), use_defaults_(false) {}

DebugUtilsMessenger::~DebugUtilsMessenger() {
  if (initialized_) {
    debug_utils_messenger_.reset();
    initialized_ = false;
  }
}

bool DebugUtilsMessenger::Init() {
  RTN_IF_MSG(false, initialized_, "Already initialized.\n");
  RTN_IF_MSG(false, !instance_, "Instance is not initialized.\n");

  if (!FindRequiredProperties({"VK_LAYER_KHRONOS_validation"}, INSTANCE_LAYER_PROP))
    RTN_MSG(false, "Missing layer VK_LAYER_KHRONOS_validation.");

  if (!FindRequiredProperties({"VK_EXT_DEBUG_UTILS_EXTENSION_NAME"}, INSTANCE_EXT_PROP))
    RTN_MSG(false, "Missing extension %s\n", VK_EXT_DEBUG_UTILS_EXTENSION_NAME);

  if (use_defaults_) {
    debug_utils_messenger_info_ = DefaultDebugUtilsMessengerInfo();
  }

  dispatch_loader_.init(*instance_, vkGetInstanceProcAddr);
  if (!dispatch_loader_.vkCreateDebugUtilsMessengerEXT) {
    RTN_MSG(false,
            "Dispatch loader has no vkCreateDebugUtilsMessengerEXT() support.\n"
            "Verify that the provided instance: %p was created with layer "
            "VK_LAYER_KHRONOS_validation.\n",
            instance_.get());
  }
  auto [r_messenger, messenger] = instance_->createDebugUtilsMessengerEXTUnique(
      debug_utils_messenger_info_, nullptr /* pAllocator */, dispatch_loader_);
  RTN_IF_VKH_ERR(false, r_messenger, "Failed to create debug messenger.\n");
  debug_utils_messenger_ = std::move(messenger);

  initialized_ = true;
  return true;
}

const vk::DebugUtilsMessengerEXT &DebugUtilsMessenger::get() const {
  return debug_utils_messenger_.get();
}

vk::DebugUtilsMessengerCreateInfoEXT DebugUtilsMessenger::debug_utils_messenger_info() {
  if (use_defaults_)
    return DefaultDebugUtilsMessengerInfo();
  return debug_utils_messenger_info_;
}

vk::DebugUtilsMessengerCreateInfoEXT DebugUtilsMessenger::DefaultDebugUtilsMessengerInfo() {
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

  return info;
}

}  // namespace vkp
