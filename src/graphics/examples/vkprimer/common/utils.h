// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_GRAPHICS_EXAMPLES_VKPRIMER_COMMON_UTILS_H_
#define SRC_GRAPHICS_EXAMPLES_VKPRIMER_COMMON_UTILS_H_

#include <string>
#include <vector>

#include <vulkan/vulkan.hpp>

#define RTN_MSG(err, ...)                          \
  {                                                \
    fprintf(stderr, "%s:%d ", __FILE__, __LINE__); \
    fprintf(stderr, __VA_ARGS__);                  \
    return err;                                    \
  }

namespace vkp {

enum SearchProp { INSTANCE_EXT_PROP, INSTANCE_LAYER_PROP, PHYS_DEVICE_EXT_PROP };

//
// Using the vkEnumerate* entrypoints, search for all elements of
// |desired_props| to look for a match.  If all elements are found,
// return true.  If any are missing, return false and populate
// |missing_props_out| with the missing properties. If nullptr is
// passed for |missing_props_out|, it will be ignored.
//
// The type of enumeration entrypoint used is selected using the
// |search_prop| parameter.  Those 3 selectable entrypoints are:
//
//   vk::enumerateInstanceExtensionProperties
//   vk::enumerateInstanceLayerProperties
//   vk::enumerateDeviceExtensionProperties
//
bool FindMatchingProperties(const std::vector<const char *> &desired_props, SearchProp search_prop,
                            vk::PhysicalDevice phys_device, const char *layer,
                            std::vector<std::string> *missing_propss_out);

// Find graphics queue families for |surface|.  Populate |queue_family_indices|
// if it is non-null.  Returns true if a graphics queue family is found.
bool FindGraphicsQueueFamilies(vk::PhysicalDevice phys_device, VkSurfaceKHR surface,
                               std::vector<uint32_t> *queue_family_indices);

// Find physical device memory property index for |properties|.
int FindMemoryIndex(const vk::PhysicalDevice &phys_dev, const uint32_t memory_type_bits,
                    const vk::MemoryPropertyFlags &properties);

// Log physical device memory properties.
void LogMemoryProperties(const vk::PhysicalDevice &phys_dev);

}  // namespace vkp

#endif  // SRC_GRAPHICS_EXAMPLES_VKPRIMER_COMMON_UTILS_H_
