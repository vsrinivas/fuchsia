// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "utils.h"

#include <cassert>
#include <cstdio>
#include <optional>
#include <unordered_set>

namespace {

static void PrintProps(const std::vector<std::string> &props) {
  for (const auto &prop : props) {
    printf("\t%s\n", prop.c_str());
  }
  printf("\n");
}

}  // namespace

namespace vkp {

//
// Enumerate properties categorically using |search_prop| and populate |props_found_set|
// with the results.  Returns false if no properties are enumerated.
//
static bool EnumerateProperties(SearchProp search_prop, vk::PhysicalDevice physical_device,
                                const char *layer,
                                std::unordered_set<std::string> *enumerated_props) {
  std::optional<vk::ResultValue<std::vector<vk::ExtensionProperties>>> rv_ext_props;
  std::optional<vk::ResultValue<std::vector<vk::LayerProperties>>> rv_layer_props;
  std::vector<vk::ExtensionProperties> ext_props;
  std::vector<vk::LayerProperties> layer_props;
  switch (search_prop) {
    case INSTANCE_EXT_PROP:
      if (layer) {
        rv_ext_props = vk::enumerateInstanceExtensionProperties(std::string(layer));
      } else {
        rv_ext_props = vk::enumerateInstanceExtensionProperties(nullptr);
      }
      RTN_IF_VKH_ERR(false, rv_ext_props->result,
                     "Failed to enumerate instance extension properties.\n");
      ext_props = rv_ext_props->value;
      break;
    case INSTANCE_LAYER_PROP:
      rv_layer_props = vk::enumerateInstanceLayerProperties();
      RTN_IF_VKH_ERR(false, rv_layer_props->result,
                     "Failed to enumerate instance layer properties.\n");
      layer_props = rv_layer_props->value;
      break;
    case PHYS_DEVICE_EXT_PROP:
      assert(physical_device && "Null physical device used for phys device property query.");
      if (layer) {
        rv_ext_props = physical_device.enumerateDeviceExtensionProperties(std::string(layer));
      } else {
        rv_ext_props = physical_device.enumerateDeviceExtensionProperties(nullptr);
      }
      RTN_IF_VKH_ERR(false, rv_ext_props->result,
                     "Failed to enumerate physical device extension properties.\n");
      ext_props = rv_ext_props->value;
      break;
  }

  if (search_prop == INSTANCE_LAYER_PROP) {
    for (auto &prop : layer_props) {
      enumerated_props->insert(std::string(prop.layerName));
    }
  } else {
    for (auto &prop : ext_props) {
      if (search_prop == PHYS_DEVICE_EXT_PROP && layer) {
        printf("Phys Dev Props: layer(%s) prop(%s)\n", layer,
               std::string(prop.extensionName).c_str());
      }
      enumerated_props->insert(std::string(prop.extensionName));
    }
  }

  return true;
}

bool FindRequiredProperties(const std::vector<const char *> &required_props, SearchProp search_prop,
                            const char *layer, vk::PhysicalDevice physical_device,
                            std::vector<std::string> *missing_props_out) {
  std::unordered_set<std::string> enumerated_props_set;

  // Match Vulkan properties.  "Vulkan properties" are those found when the
  // layer argument is set to null.
  bool success = EnumerateProperties(search_prop, physical_device, nullptr, &enumerated_props_set);

  if (!success) {
    if (missing_props_out) {
      missing_props_out->insert(missing_props_out->end(), required_props.begin(),
                                required_props.end());
    }
    RTN_MSG(false, "Unable to match vulkan properties.\n");
  }

  // Match |layer| specific properties.
  if (search_prop != INSTANCE_LAYER_PROP && layer &&
      enumerated_props_set.size() != required_props.size()) {
    success = EnumerateProperties(search_prop, physical_device, layer, &enumerated_props_set);
  }

  bool has_missing_props = false;

  // Add missing properties to |missing_props_out| if required.
  if (missing_props_out) {
    for (const auto &prop : required_props) {
      auto iter = enumerated_props_set.find(prop);
      if (iter == enumerated_props_set.end()) {
        missing_props_out->emplace_back(prop);
        has_missing_props = true;
      }
    }
  }

  if (has_missing_props) {
    std::string category{};
    switch (search_prop) {
      case INSTANCE_EXT_PROP:
        category = "instance extension";
        break;
      case INSTANCE_LAYER_PROP:
        category = "instance layer";
        break;
      case PHYS_DEVICE_EXT_PROP:
        category = "physical device extension";
        break;
    }
    fprintf(stderr, "Missing %s properties\n", category.c_str());
    PrintProps(*missing_props_out);
  }

  return (success && !has_missing_props);
}

bool FindQueueFamilyIndex(vk::PhysicalDevice physical_device, VkSurfaceKHR surface,
                          vk::QueueFlags queue_flags, uint32_t *queue_family_index) {
  auto queue_families = physical_device.getQueueFamilyProperties();
  for (uint32_t i = 0; i < queue_families.size(); ++i) {
    if (surface) {
      auto [r_present_support, present_support] = physical_device.getSurfaceSupportKHR(i, surface);
      if (vk::Result::eSuccess != r_present_support) {
        continue;
      }
    }

    const auto &queue_family = queue_families[i];
    if ((queue_family.queueCount > 0) && (queue_family.queueFlags & queue_flags)) {
      if (queue_family_index) {
        *queue_family_index = i;
      }
      return true;
    }
  }
  RTN_MSG(false, "No matching queue family index found.\n");
}

int FindMemoryIndex(const vk::PhysicalDevice &phys_dev, const uint32_t memory_type_bits,
                    const vk::MemoryPropertyFlags &memory_prop_flags) {
  vk::PhysicalDeviceMemoryProperties memory_props;
  phys_dev.getMemoryProperties(&memory_props);

  for (uint32_t i = 0; i < memory_props.memoryTypeCount; i++) {
    if ((memory_type_bits & (1 << i)) &&
        (memory_props.memoryTypes[i].propertyFlags & memory_prop_flags) == memory_prop_flags) {
      return i;
    }
  }

  RTN_MSG(-1, "Error: Unable to find memory property index.");
}

void LogMemoryProperties(const vk::PhysicalDevice &phys_dev) {
  vk::PhysicalDeviceMemoryProperties memory_props;
  phys_dev.getMemoryProperties(&memory_props);
  const uint32_t memory_type_ct = memory_props.memoryTypeCount;
  const vk::MemoryType *types = memory_props.memoryTypes;

  printf("\nMemory Types: %d\n", memory_type_ct);
  for (uint32_t i = 0; i < memory_type_ct; i++) {
    const vk::MemoryType &type = types[i];
    printf("\tHeap Index: %d\n", type.heapIndex);
    if (type.propertyFlags & vk::MemoryPropertyFlagBits::eDeviceLocal)
      printf("\t\tDevice Local\n");
    if (type.propertyFlags & vk::MemoryPropertyFlagBits::eHostVisible)
      printf("\t\tHost Visible\n");
    if (type.propertyFlags & vk::MemoryPropertyFlagBits::eHostCoherent)
      printf("\t\tHost Coherent\n");
    if (type.propertyFlags & vk::MemoryPropertyFlagBits::eHostCached)
      printf("\t\tHost Cached\n");
    if (type.propertyFlags & vk::MemoryPropertyFlagBits::eLazilyAllocated)
      printf("\t\tLazily Allocated\n");
    if (type.propertyFlags & vk::MemoryPropertyFlagBits::eProtected)
      printf("\t\tProtected\n");
    if (type.propertyFlags & vk::MemoryPropertyFlagBits::eDeviceCoherentAMD)
      printf("\t\tDevice Coherent AMD\n");
    if (type.propertyFlags & vk::MemoryPropertyFlagBits::eDeviceUncachedAMD)
      printf("\t\tDevice Uncached AMD\n");
  }
  printf("\n");
}

}  // namespace vkp
