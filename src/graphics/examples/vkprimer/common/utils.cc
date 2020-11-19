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
static bool EnumerateProperties(SearchProp search_prop, vk::PhysicalDevice phys_device,
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
      assert(phys_device && "Null phys device used for phys device property query.");
      if (layer) {
        rv_ext_props = phys_device.enumerateDeviceExtensionProperties(std::string(layer));
      } else {
        rv_ext_props = phys_device.enumerateDeviceExtensionProperties(nullptr);
      }
      RTN_IF_VKH_ERR(false, rv_ext_props->result,
                     "Failed to enumerate device extension properties.\n");
      ext_props = rv_ext_props->value;
      break;
  }

  if (search_prop == INSTANCE_LAYER_PROP) {
    for (auto &prop : layer_props) {
      enumerated_props->insert(std::string(prop.layerName));
    }
  } else {
    for (auto &prop : ext_props) {
      enumerated_props->insert(std::string(prop.extensionName));
    }
  }

  return true;
}

bool FindRequiredProperties(const std::vector<const char *> &required_props, SearchProp search_prop,
                            vk::PhysicalDevice phys_device, const char *layer,
                            std::vector<std::string> *missing_props_out) {
  std::unordered_set<std::string> enumerated_props_set;

  // Match Vulkan properties.  "Vulkan properties" are those
  // found when the layer argument is set to null.
  bool success = EnumerateProperties(search_prop, phys_device, nullptr, &enumerated_props_set);

  if (!success) {
    if (missing_props_out) {
      missing_props_out->insert(missing_props_out->end(), required_props.begin(),
                                required_props.end());
    }
    RTN_MSG(false, "Unable to match vulkan properties.\n");
  }

  // Match layer properties.
  if (search_prop != INSTANCE_LAYER_PROP && layer &&
      enumerated_props_set.size() != required_props.size()) {
    success = EnumerateProperties(search_prop, phys_device, layer, &enumerated_props_set);
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
    PrintProps(*missing_props_out);
  }

  return (success && !has_missing_props);
}

bool FindGraphicsQueueFamilies(vk::PhysicalDevice phys_device, VkSurfaceKHR surface,
                               std::vector<uint32_t> *queue_family_indices) {
  auto queue_families = phys_device.getQueueFamilyProperties();
  int queue_family_index = 0;
  for (const auto &queue_family : queue_families) {
    auto [r_present_support, present_support] =
        phys_device.getSurfaceSupportKHR(queue_family_index, surface);
    RTN_IF_VKH_ERR(false, r_present_support, "Failed to get surface present support.\n");

    if ((queue_family.queueCount > 0) && (queue_family.queueFlags & vk::QueueFlagBits::eGraphics) &&
        present_support) {
      if (queue_family_indices) {
        queue_family_indices->emplace_back(queue_family_index);
      }
      return true;
    }
    queue_family_index++;
  }
  RTN_MSG(false, "No queue family indices found.\n");
}

int FindMemoryIndex(const vk::PhysicalDevice &phys_dev, const uint32_t memory_type_bits,
                    const vk::MemoryPropertyFlags &desired_props) {
  vk::PhysicalDeviceMemoryProperties memory_props;
  phys_dev.getMemoryProperties(&memory_props);

  for (uint32_t i = 0; i < memory_props.memoryTypeCount; i++) {
    if ((memory_type_bits & (1 << i)) &&
        (memory_props.memoryTypes[i].propertyFlags & desired_props) == desired_props) {
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
