// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_LIB_VULKAN_TESTS_VKPRIMER_COMMON_UTILS_H_
#define GARNET_LIB_VULKAN_TESTS_VKPRIMER_COMMON_UTILS_H_

#include <vulkan/vulkan.h>

#include <string>
#include <vector>

#define RTN_MSG(err, ...)                          \
  {                                                \
    fprintf(stderr, "%s:%d ", __FILE__, __LINE__); \
    fprintf(stderr, __VA_ARGS__);                  \
    return err;                                    \
  }

enum SearchProp {
  INSTANCE_EXT_PROP,
  INSTANCE_LAYER_PROP,
  PHYS_DEVICE_EXT_PROP
};

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
//   vkEnumerateInstanceExtensionProperties
//   vkEnumerateInstanceLayerProperties
//   vkEnumerateDeviceExtensionProperties
//
bool FindMatchingProperties(const std::vector<const char *> &desired_props,
                            SearchProp search_prop,
                            const VkPhysicalDevice *phys_device,
                            const char *layer,
                            std::vector<std::string> *missing_propss_out);

#endif  // GARNET_LIB_VULKAN_TESTS_VKPRIMER_COMMON_UTILS_H_
