// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <set>
#include <string>
#include <vulkan/vulkan.hpp>

#include "lib/fxl/memory/ref_counted.h"

namespace escher {

class VulkanInstance;
using VulkanInstancePtr = fxl::RefPtr<VulkanInstance>;

// Convenient wrapper for creating and managing the lifecycle of a VkInstance
// that is suitable for use by Escher.
class VulkanInstance : public fxl::RefCountedThreadSafe<VulkanInstance> {
 public:
  // Parameters used to construct a new Vulkan Instance.
  struct Params {
    std::set<std::string> layer_names{"VK_LAYER_LUNARG_standard_validation"};
    std::set<std::string> extension_names;
    bool requires_surface = true;
  };

  // Contains dynamically-obtained addresses of instance-specific functions.
  struct ProcAddrs {
    ProcAddrs(vk::Instance instance, bool requires_surface);

    PFN_vkCreateDebugReportCallbackEXT CreateDebugReportCallbackEXT = nullptr;
    PFN_vkDestroyDebugReportCallbackEXT DestroyDebugReportCallbackEXT = nullptr;
    PFN_vkGetPhysicalDeviceSurfaceSupportKHR
        GetPhysicalDeviceSurfaceSupportKHR = nullptr;
  };

  // Constructor.
  static fxl::RefPtr<VulkanInstance> New(Params params);

  ~VulkanInstance();

  // Enumerate the available instance layers.  Return true if all required
  // layers are present, and false otherwise.
  static bool ValidateLayers(const std::set<std::string>& required_layer_names);

  // Enumerate the available instance extensions.  Return true if all required
  // extensions are present, and false otherwise.
  static bool ValidateExtensions(
      const std::set<std::string>& required_extension_names);

  vk::Instance vk_instance() const { return instance_; }

  // Return the parameterss that were used to create this instance.
  const Params& params() const { return params_; }

  // Return per-instance functions that were dynamically looked up.
  const ProcAddrs& proc_addrs() const { return proc_addrs_; }

 private:
  VulkanInstance(vk::Instance instance, Params params);

  vk::Instance instance_;
  Params params_;
  ProcAddrs proc_addrs_;
};

};  // namespace escher
