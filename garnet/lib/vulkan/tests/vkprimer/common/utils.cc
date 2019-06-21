// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "utils.h"

#include <unordered_set>

static void PrintProps(const std::vector<std::string> &props) {
  for (const auto &prop : props) {
    printf("\t%s\n", prop.c_str());
  }
  printf("\n");
}

static bool MatchProperties(const std::vector<const char *> &desired_props,
                            SearchProp search_prop,
                            const VkPhysicalDevice &phys_device,
                            const char *layer,
                            std::unordered_set<std::string> *props_found_set) {
  uint32_t num_props = 0;
  // Count instance properties.
  VkResult err;
  switch (search_prop) {
    case INSTANCE_EXT_PROP:
      err = vkEnumerateInstanceExtensionProperties(layer, &num_props, nullptr);
      break;
    case INSTANCE_LAYER_PROP:
      err = vkEnumerateInstanceLayerProperties(&num_props, nullptr);
      break;
    case PHYS_DEVICE_EXT_PROP:
      err = vkEnumerateDeviceExtensionProperties(phys_device, layer, &num_props,
                                                 nullptr);
      break;
  }

  if (VK_SUCCESS != err) {
    RTN_MSG(false, "VK Error: 0x%x - Unable to count properties %s\n", err,
            (layer == nullptr) ? "" : layer);
  }

  // Gather instance properties.
  std::vector<VkExtensionProperties> ext_props(num_props);
  std::vector<VkLayerProperties> layer_props(num_props);

  switch (search_prop) {
    case INSTANCE_EXT_PROP:
      err = vkEnumerateInstanceExtensionProperties(layer, &num_props,
                                                   ext_props.data());
      break;
    case INSTANCE_LAYER_PROP:
      err = vkEnumerateInstanceLayerProperties(&num_props, layer_props.data());
      break;
    case PHYS_DEVICE_EXT_PROP:
      err = vkEnumerateDeviceExtensionProperties(phys_device, layer, &num_props,
                                                 ext_props.data());
      break;
  }

  if (VK_SUCCESS != err) {
    RTN_MSG(false, "VK Error: 0x%x - Unable to gather properties %s\n", err,
            (layer == nullptr) ? "" : layer);
  }

  if (search_prop == INSTANCE_LAYER_PROP) {
    for (auto &prop : layer_props) {
      props_found_set->insert(std::string(prop.layerName));
    }
  } else {
    for (auto &prop : ext_props) {
      props_found_set->insert(std::string(prop.extensionName));
    }
  }

  return true;
}

bool FindMatchingProperties(const std::vector<const char *> &desired_props,
                            SearchProp search_prop,
                            const VkPhysicalDevice *phys_device,
                            const char *layer,
                            std::vector<std::string> *missing_props_out) {
  std::unordered_set<std::string> props_found_set;

  // Match Vulkan properties.  "Vulkan properties" are those
  // found when the layer argument is set to null.
  bool success = MatchProperties(desired_props, search_prop, *phys_device,
                                 nullptr, &props_found_set);

  if (!success) {
    if (missing_props_out) {
      missing_props_out->insert(missing_props_out->end(), desired_props.begin(),
                                desired_props.end());
    }
    RTN_MSG(false, "Unable to match vulkan properties.\n");
  }

  // Match layer properties.
  if (search_prop != INSTANCE_LAYER_PROP && layer &&
      props_found_set.size() != desired_props.size()) {
    success = MatchProperties(desired_props, search_prop, *phys_device, layer,
                              &props_found_set);
  }

  if (missing_props_out) {
    for (const auto &prop : desired_props) {
      auto iter = props_found_set.find(prop);
      if (iter == props_found_set.end()) {
        missing_props_out->emplace_back(*iter);
      }
    }
  }

  if (missing_props_out && !missing_props_out->empty()) {
    PrintProps(*missing_props_out);
  }

  return props_found_set.size() == desired_props.size();
}
