// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "spinel_vk_device_config_utils.h"

#include <stdio.h>

#include "hotsort_vk.h"
#include "spinel/spinel_vk.h"
#include "spinel_vk_find_target.h"
#include "tests/common/utils.h"  // For ASSERT() macros.

#define DEBUG 0

bool
spinel_vk_device_config_callback(void *               opaque,
                                 VkInstance           instance,
                                 VkPhysicalDevice     physical_device,
                                 vk_device_config_t * device_config)
{
  spinel_vk_device_configuration_t * spinel_config = (spinel_vk_device_configuration_t *)opaque;

  // First check that we have working targets for this physical device.
  VkPhysicalDeviceProperties props;
  vkGetPhysicalDeviceProperties(physical_device, &props);

  // Check vendor/device ID if needed.
  uint32_t wanted_vendor = spinel_config->wanted_vendor_id;
  if (wanted_vendor && wanted_vendor != props.vendorID)
    return false;

  uint32_t wanted_device = spinel_config->wanted_device_id;
  if (wanted_device && wanted_device != props.deviceID)
    return false;

  // Verify that there are Spinel + Hotsort targets for this device.
  char error[256];
  if (!spn_vk_find_target(props.vendorID,
                          props.deviceID,
                          &spinel_config->spinel_target,
                          &spinel_config->hotsort_target,
                          error,
                          sizeof(error)))
    {
      fprintf(stderr, "%s\n", error);
      return false;
    }

  // Setup the |features| field and its extension chain.
  device_config->features = (const VkPhysicalDeviceFeatures2){
    .sType    = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2,
    .features = {},
  };

  size_t ext_chain_storage_size = sizeof(spinel_config->ext_chain_storage);

  size_t ext_chain_size = 0u;
  // NOTE: the call below is expected to fail with SPN_ERROR_PARTIAL_TARGET_REQUIREMENTS.
  spn_vk_target_get_feature_structures(spinel_config->spinel_target,
                                       &ext_chain_size,
                                       NULL);

  ASSERT(ext_chain_size >= sizeof(VkBaseOutStructure));  // Sanity check
  ASSERT_MSG(ext_chain_size <= ext_chain_storage_size,
             "Please increase the size of |spinel_vk_device_configuration_t::ext_chain_storage|\n");

  ASSERT_MSG(spn_vk_target_get_feature_structures(spinel_config->spinel_target,
                                                  &ext_chain_size,
                                                  &spinel_config->ext_chain_storage) == SPN_SUCCESS,
             "Could not get features structure for target!\n");

  device_config->features.pNext = spinel_config->ext_chain_storage;

  // Now, a first set of calls to grab the _sizes_ of the target requirements arrays.
  // NOTE: These calls are expected to fail with SPN_ERROR_PARTIAL_TARGET_REQUIREMENTS.
  struct spn_vk_target_requirements spinel_reqs = {};
  spn_vk_target_get_requirements(spinel_config->spinel_target, &spinel_reqs);

  struct hotsort_vk_target_requirements hotsort_reqs = {};
  hotsort_vk_target_get_requirements(spinel_config->hotsort_target, &hotsort_reqs);

  uint32_t num_extensions = spinel_reqs.ext_name_count + hotsort_reqs.ext_name_count;
  ASSERT_MSG(num_extensions <= VK_DEVICE_CONFIG_MAX_EXTENSIONS,
             "Too many extensions in target requirements (%u + %u > %u)",
             spinel_reqs.ext_name_count,
             hotsort_reqs.ext_name_count,
             VK_DEVICE_CONFIG_MAX_EXTENSIONS);

  VkDeviceQueueCreateInfo queues[spinel_reqs.qci_count];
  spinel_reqs.qcis       = queues;
  spinel_reqs.ext_names  = device_config->extensions_names;
  spinel_reqs.pdf2       = &device_config->features;
  hotsort_reqs.ext_names = device_config->extensions_names + spinel_reqs.ext_name_count;
  hotsort_reqs.pdf       = &device_config->features.features;

  // A second set of calls to get the actual requirements.
  ASSERT_MSG(
    spn_vk_target_get_requirements(spinel_config->spinel_target, &spinel_reqs) == SPN_SUCCESS,
    "Cannot get spinel target requirements! (%X:%X)\n",
    props.vendorID,
    props.deviceID);

  ASSERT_MSG(hotsort_vk_target_get_requirements(spinel_config->hotsort_target, &hotsort_reqs),
             "Cannot get hotsort target requirements! (%X:%X)\n",
             props.vendorID,
             props.deviceID);

  device_config->extensions_count = num_extensions;

  // For now, Spinel only supports a single queue so keep things simple.
  ASSERT_MSG(spinel_reqs.qci_count == 1,
             "Unsupported number of queues: %u\n",
             spinel_reqs.qci_count);

  device_config->required_queues = VK_QUEUE_COMPUTE_BIT;
  device_config->physical_device = physical_device;

#if DEBUG
  fprintf(stderr, "Spinel device config:\n");
  fprintf(stderr, "  spinel target:  %p\n", spinel_config->spinel_target);
  fprintf(stderr, "  hotsort target: %p\n", spinel_config->hotsort_target);
  fprintf(stderr, "  extensions (%u):\n", device_config->extensions_count);
  for (uint32_t nn = 0; nn < device_config->extensions_count; ++nn)
    {
      fprintf(stderr, "    %s\n", device_config->extensions_names[nn]);
    }
  fprintf(stderr, "  physical device features:\n");
#define CHECK_FEATURE(feature)                                                                     \
  do                                                                                               \
    {                                                                                              \
      if (device_config->features.feature == VK_TRUE)                                              \
        {                                                                                          \
          fprintf(stderr, "    %14s:  true\n", #feature);                                          \
        }                                                                                          \
    }                                                                                              \
  while (0);

  CHECK_FEATURE(shaderInt64)
  CHECK_FEATURE(shaderFloat64)

#undef CHECK_FEATURE
#endif  // DEBUG

  return true;
}

struct spn_vk_environment
vk_app_state_get_spinel_environment(const vk_app_state_t * app_state)
{
  return (struct spn_vk_environment){
    .d    = app_state->d,
    .ac   = app_state->ac,
    .pc   = app_state->pc,
    .pd   = app_state->pd,
    .pdmp = app_state->pdmp,
    .qfi  = app_state->compute_qfi,
  };
}

static void
size_to_string(size_t size, char * buffer, size_t buffer_size)
{
  if (size < 65536)
    {
      snprintf(buffer, buffer_size, "%u", (unsigned)size);
    }
  else if (size < 1024 * 1024)
    {
      snprintf(buffer, buffer_size, "%.1f kiB", size / 1024.);
    }
  else if (size < 1024 * 1024 * 1024)
    {
      snprintf(buffer, buffer_size, "%.1f MiB", size / (1024. * 1024.));
    }
  else
    {
      snprintf(buffer, buffer_size, "%1.f GiB", size / (1024. * 1024. * 1024.));
    }
}

extern void
spn_vk_environment_print(const struct spn_vk_environment * environment)
{
  fprintf(stderr, "Spinel environment:\n");
  fprintf(stderr, "  device:          %p\n", environment->d);
  fprintf(stderr, "  allocator:       %p\n", environment->ac);
  fprintf(stderr, "  pipeline cache:  %p\n", environment->pc);
  fprintf(stderr, "  physical device: %p\n", environment->pd);
  fprintf(stderr, "  memory properties:\n");

  for (uint32_t n = 0; n < environment->pdmp.memoryHeapCount; ++n)
    {
      uint32_t flags = environment->pdmp.memoryHeaps[n].flags;
      char     size_str[32];
      size_to_string(environment->pdmp.memoryHeaps[n].size, size_str, sizeof(size_str));
      printf("      heap index=%-2d size=%-8s flags=0x%08x", n, size_str, flags);
#define FLAG_BIT(name)                                                                             \
  do                                                                                               \
    {                                                                                              \
      if (flags & VK_MEMORY_HEAP_##name##_BIT)                                                     \
        printf(" " #name);                                                                         \
    }                                                                                              \
  while (0)
      FLAG_BIT(DEVICE_LOCAL);
      FLAG_BIT(MULTI_INSTANCE);
#undef FLAG_BIT
      printf("\n");
    }
  for (uint32_t n = 0; n < environment->pdmp.memoryTypeCount; ++n)
    {
      uint32_t flags = environment->pdmp.memoryTypes[n].propertyFlags;
      printf("      type index=%-2d heap=%-2d flags=0x%08x",
             n,
             environment->pdmp.memoryTypes[n].heapIndex,
             flags);
#define FLAG_BIT(name)                                                                             \
  do                                                                                               \
    {                                                                                              \
      if (flags & VK_MEMORY_PROPERTY_##name##_BIT)                                                 \
        printf(" " #name);                                                                         \
    }                                                                                              \
  while (0)
      FLAG_BIT(DEVICE_LOCAL);
      FLAG_BIT(HOST_VISIBLE);
      FLAG_BIT(HOST_COHERENT);
      FLAG_BIT(HOST_CACHED);
      FLAG_BIT(LAZILY_ALLOCATED);
      FLAG_BIT(PROTECTED);
#undef FLAG_BIT
      printf("\n");
    }

  fprintf(stderr, "  queue family:    %u\n", environment->qfi);
}
