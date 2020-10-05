// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//
//
//

#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

//
//
//

#include "common/macros.h"
#include "common/vk/assert.h"
#include "common/vk/cache.h"
#include "common/vk/find_mem_type_idx.h"
#include "common/vk/find_validation_layer.h"

//
//
//

#if defined(HOTSORT_VK_SHADER_INFO_AMD_STATISTICS) ||                                              \
  defined(HOTSORT_VK_SHADER_INFO_AMD_DISASSEMBLY)
#include "common/vk/shader_info_amd.h"
#endif

//
//
//

#include "hotsort_vk.h"

//
// Compile-time images of HotSort targets
//

#include "hs_amd_gcn3_u32/hs_target.h"
#include "hs_amd_gcn3_u64/hs_target.h"
#include "hs_arm_bifrost4_u32/hs_target.h"
#include "hs_arm_bifrost4_u64/hs_target.h"
#include "hs_arm_bifrost8_u32/hs_target.h"
#include "hs_arm_bifrost8_u64/hs_target.h"
#include "hs_google_swiftshader_u32/hs_target.h"
#include "hs_google_swiftshader_u64/hs_target.h"
#include "hs_intel_gen8_u32/hs_target.h"
#include "hs_intel_gen8_u64/hs_target.h"
#include "hs_nvidia_sm35_u32/hs_target.h"
#include "hs_nvidia_sm35_u64/hs_target.h"

//
// This test reads the target's architectural configuration
//

#include "targets/hotsort_vk_target.h"

//
// Define a platform-specific prefix
//

#ifdef __Fuchsia__
#define VK_PIPELINE_CACHE_PREFIX_STRING "/cache/."
#else
#define VK_PIPELINE_CACHE_PREFIX_STRING "."
#endif

//
// clang-format off
//

#define HS_BENCH_LOOPS      100
#define HS_BENCH_WARMUP     100
#define HS_BENCH_LOOPS_CPU  8

//
// clang-format on
//

char const *
hs_cpu_sort_u32(uint32_t * a, uint32_t const count, double * const cpu_ns);
char const *
hs_cpu_sort_u64(uint64_t * a, uint32_t const count, double * const cpu_ns);

//
//
//

static char const *
hs_cpu_sort(void * sorted_h, uint32_t const hs_words, uint32_t const count, double * const cpu_ns)
{
  if (hs_words == 1)
    return hs_cpu_sort_u32(sorted_h, count, cpu_ns);
  else
    return hs_cpu_sort_u64(sorted_h, count, cpu_ns);
}

static void
hs_transpose_slabs_u32(uint32_t const hs_words,
                       uint32_t const hs_width,
                       uint32_t const hs_height,
                       uint32_t *     vout_h,
                       uint32_t const count)
{
  uint32_t const   slab_keys  = hs_width * hs_height;
  size_t const     slab_size  = sizeof(uint32_t) * hs_words * slab_keys;
  uint32_t * const slab       = malloc(slab_size);
  uint32_t         slab_count = count / slab_keys;

  while (slab_count-- > 0)
    {
      memcpy(slab, vout_h, slab_size);

      for (uint32_t row = 0; row < hs_height; row++)
        for (uint32_t col = 0; col < hs_width; col++)
          vout_h[col * hs_height + row] = slab[row * hs_width + col];

      vout_h += slab_keys;
    }

  free(slab);
}

static void
hs_transpose_slabs_u64(uint32_t const hs_words,
                       uint32_t const hs_width,
                       uint32_t const hs_height,
                       uint64_t *     vout_h,
                       uint32_t const count)
{
  uint32_t const   slab_keys  = hs_width * hs_height;
  size_t const     slab_size  = sizeof(uint32_t) * hs_words * slab_keys;
  uint64_t * const slab       = malloc(slab_size);
  uint32_t         slab_count = count / slab_keys;

  while (slab_count-- > 0)
    {
      memcpy(slab, vout_h, slab_size);

      for (uint32_t row = 0; row < hs_height; row++)
        for (uint32_t col = 0; col < hs_width; col++)
          vout_h[col * hs_height + row] = slab[row * hs_width + col];

      vout_h += slab_keys;
    }

  free(slab);
}

static void
hs_transpose_slabs(uint32_t const hs_words,
                   uint32_t const hs_width,
                   uint32_t const hs_height,
                   void *         vout_h,
                   uint32_t const count)
{
  if (hs_words == 1)
    hs_transpose_slabs_u32(hs_words, hs_width, hs_height, vout_h, count);
  else
    hs_transpose_slabs_u64(hs_words, hs_width, hs_height, vout_h, count);
}

//
//
//

#if 1

static uint32_t
hs_rand_u32()
{
  static uint32_t seed = 0xDEADBEEF;

  // Numerical Recipes
  seed = seed * 1664525 + 1013904223;

  return seed;
}

#else

static uint32_t
hs_rand_u32()
{
  static uint32_t const pattern[] = { 2, 0, 3, 1 };
  static uint32_t       next      = 0;

  return pattern[next++ % ARRAY_LENGTH_MACRO(pattern)];
}

#endif

//
//
//

static void
hs_fill_rand(uint32_t * vin_h, uint32_t const count, uint32_t const words, uint32_t rand_bits)
{
#if 1
  uint32_t const word_mask = words - 1;

  union
  {
    uint64_t qword;
    uint32_t dwords[2];
  } const rand_mask = { .qword = (rand_bits == 0) ? 0 : (UINT64_MAX >> (64 - rand_bits)) };

#ifndef NDEBUG
  fprintf(stderr, "rand_mask(%u) = 0x%016" PRIX64 "\n", rand_bits, rand_mask.qword);
#endif

  for (uint32_t ii = 0; ii < count * words; ii++)
    vin_h[ii] = hs_rand_u32() & rand_mask.dwords[ii & word_mask];
#elif 0  // increasing order
  memset(vin_h, 0, count * words * sizeof(uint32_t));
  for (uint32_t ii = 0; ii < count; ii++)
    vin_h[ii * words] = ii;
#else  // reverse order
  memset(vin_h, 0, count * words * sizeof(uint32_t));
  for (uint32_t ii = 0; ii < count; ii++)
    vin_h[ii * words] = count - 1 - ii;
#endif
}

//
//
//

#if !defined(NDEBUG) && defined(HS_VK_DUMP_SLABS)

static void
hs_debug_u32(uint32_t const   hs_width,
             uint32_t const   hs_height,
             uint32_t const * vout_h,
             uint32_t const   count)
{
  uint32_t const slab_keys = hs_width * hs_height;
  uint32_t const slabs     = (count + slab_keys - 1) / slab_keys;

  for (uint32_t ss = 0; ss < slabs; ss++)
    {
      fprintf(stderr, "%u\n", ss);
      for (uint32_t cc = 0; cc < hs_height; cc++)
        {
          for (uint32_t rr = 0; rr < hs_width; rr++)
            fprintf(stderr, "%8" PRIX32 " ", *vout_h++);
          fprintf(stderr, "\n");
        }
    }
}

static void
hs_debug_u64(uint32_t const   hs_width,
             uint32_t const   hs_height,
             uint64_t const * vout_h,
             uint32_t const   count)
{
  uint32_t const slab_keys = hs_width * hs_height;
  uint32_t const slabs     = (count + slab_keys - 1) / slab_keys;

  for (uint32_t ss = 0; ss < slabs; ss++)
    {
      fprintf(stderr, "%u\n", ss);
      for (uint32_t cc = 0; cc < hs_height; cc++)
        {
          for (uint32_t rr = 0; rr < hs_width; rr++)
            fprintf(stderr, "%16" PRIX64 " ", *vout_h++);
          fprintf(stderr, "\n");
        }
    }
}

#endif

//
//
//

bool
is_matching_device(VkPhysicalDeviceProperties const * const phy_device_props,
                   struct hotsort_vk_target const ** const  hs_target,
                   uint32_t const                           vendor_id,
                   uint32_t const                           device_id,
                   uint32_t const                           key_val_words)
{
  if ((phy_device_props->vendorID != vendor_id) || (phy_device_props->deviceID != device_id))
    return false;

  switch (vendor_id)
    {
        case 0x10DE: {
          //
          // NVIDIA SM35+
          //
          // FIXME -- for now, the kernels in this app are targeting
          // sm_35+ devices.  You could add some rigorous rejection by
          // device id here...
          //
          if (key_val_words == 1)
            *hs_target = hs_nvidia_sm35_u32;
          else
            *hs_target = hs_nvidia_sm35_u64;

          return true;
        }
        case 0x1002: {
          //
          // AMD GCN3+
          //
          if (key_val_words == 1)
            *hs_target = hs_amd_gcn3_u32;
          else
            *hs_target = hs_amd_gcn3_u64;

          return true;
        }
        case 0x1AE0: {
          if (device_id == 0xC0DE)
            {
              //
              // GOOGLE SWIFTSHADER
              //
              if (key_val_words == 1)
                *hs_target = hs_google_swiftshader_u32;
              else
                *hs_target = hs_google_swiftshader_u64;

              return true;
            }
          else
            {
              return false;
            }
        }
        case 0x8086: {
          //
          // INTEL GEN8+
          //
          // FIXME -- for now, the kernels in this app are targeting GEN8+
          // devices -- this does *not* include variants of GEN9LP+
          // "Apollo Lake" because that device has a different
          // architectural "shape" than GEN8 GTx.  You could add some
          // rigorous rejection by device id here...
          //
          if (key_val_words == 1)
            *hs_target = hs_intel_gen8_u32;
          else
            *hs_target = hs_intel_gen8_u64;

          return true;
        }
        case 0x13B5: {
          if (device_id == 0x70930000)
            {
              //
              // ARM BIFROST4
              //
              if (key_val_words == 1)
                *hs_target = hs_arm_bifrost4_u32;
              else
                *hs_target = hs_arm_bifrost4_u64;

              return true;
            }
          else if (device_id == 0x72120000)
            {
              //
              // ARM BIFROST8
              //
              if (key_val_words == 1)
                *hs_target = hs_arm_bifrost8_u32;
              else
                *hs_target = hs_arm_bifrost8_u64;

              return true;
            }
          else
            {
              return false;
            }
        }
        default: {
          return false;
        }
    }
}

//
//
//

int
main(int argc, char const * argv[])
{
  //
  // select the target by vendor and device id
  //
  uint32_t const vendor_id     = (argc <= 1) ? UINT32_MAX : (uint32_t)strtoul(argv[1], NULL, 16);
  uint32_t const device_id     = (argc <= 2) ? UINT32_MAX : (uint32_t)strtoul(argv[2], NULL, 16);
  uint32_t const key_val_words = (argc <= 3) ? 1 : (uint32_t)strtoul(argv[3], NULL, 0);

  if ((key_val_words != 1) && (key_val_words != 2))
    {
      fprintf(stderr, "Key/Val Words must be 1 or 2\n");
      exit(EXIT_FAILURE);
    }

  //
  // create a Vulkan instances
  //
  VkApplicationInfo const app_info = {

    .sType              = VK_STRUCTURE_TYPE_APPLICATION_INFO,
    .pNext              = NULL,
    .pApplicationName   = "Fuchsia HotSort/VK Bench",
    .applicationVersion = 0,
    .pEngineName        = "Fuchsia HotSort/VK",
    .engineVersion      = 0,
    .apiVersion         = VK_API_VERSION_1_1
  };

  char const * const instance_enabled_layers[]     = { "VK_LAYER_KHRONOS_validation" };
  char const * const instance_enabled_extensions[] = { VK_EXT_DEBUG_UTILS_EXTENSION_NAME };

  VkInstanceCreateInfo const instance_info = {

    .sType                   = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
    .pNext                   = NULL,
    .flags                   = 0,
    .pApplicationInfo        = &app_info,
    .enabledLayerCount       = ARRAY_LENGTH_MACRO(instance_enabled_layers),
    .ppEnabledLayerNames     = instance_enabled_layers,
    .enabledExtensionCount   = ARRAY_LENGTH_MACRO(instance_enabled_extensions),
    .ppEnabledExtensionNames = instance_enabled_extensions
  };

  VkInstance instance;

  vk(CreateInstance(&instance_info, NULL, &instance));

  //
  // acquire all physical devices and select a match
  //
  uint32_t phy_device_count;

  vk(EnumeratePhysicalDevices(instance, &phy_device_count, NULL));

  VkPhysicalDevice * phy_devices = malloc(phy_device_count * sizeof(*phy_devices));

  vk(EnumeratePhysicalDevices(instance, &phy_device_count, phy_devices));

  VkPhysicalDevice           phy_device = VK_NULL_HANDLE;
  VkPhysicalDeviceProperties phy_device_props;

  struct hotsort_vk_target const * hs_target;

  for (uint32_t ii = 0; ii < phy_device_count; ii++)
    {
      VkPhysicalDeviceProperties tmp;

      vkGetPhysicalDeviceProperties(phy_devices[ii], &tmp);

      bool const is_match =
        is_matching_device(&tmp, &hs_target, vendor_id, device_id, key_val_words);

      fprintf(stderr,
              "%c %4X : %4X : %s\n",
              is_match ? '*' : ' ',
              tmp.vendorID,
              tmp.deviceID,
              tmp.deviceName);

      if (is_match)
        {
          phy_device = phy_devices[ii];
          memcpy(&phy_device_props, &tmp, sizeof(tmp));
        }
    }

  if (phy_device == VK_NULL_HANDLE)
    {
      fprintf(stderr, "Device %04X:%08X not found.\n", vendor_id, device_id);

      return EXIT_FAILURE;
    }

  free(phy_devices);

  //
  // get rest of command line
  //
  uint32_t const slab_size = hs_target->config.slab.height << hs_target->config.slab.width_log2;

  uint32_t const count_lo   = (argc <= 4) ? slab_size : (uint32_t)strtoul(argv[4], NULL, 0);
  uint32_t const count_hi   = (argc <= 5) ? count_lo : (uint32_t)strtoul(argv[5], NULL, 0);
  uint32_t const count_step = (argc <= 6) ? count_lo : (uint32_t)strtoul(argv[6], NULL, 0);
  uint32_t const loops      = (argc <= 7) ? HS_BENCH_LOOPS : (uint32_t)strtoul(argv[7], NULL, 0);
  uint32_t const warmup     = (argc <= 8) ? HS_BENCH_WARMUP : (uint32_t)strtoul(argv[8], NULL, 0);
  bool const     linearize  = (argc <= 9) ? true : strtoul(argv[9], NULL, 0) != 0;
  bool const     verify     = (argc <= 10) ? true : strtoul(argv[10], NULL, 0) != 0;
  uint32_t const rand_bits =
    (argc <= 11) ? key_val_words * 32 : (uint32_t)strtoul(argv[11], NULL, 0);

  //
  //
  //
  if (count_lo == 0)
    {
      fprintf(stderr, "Key count must be >= 1\n");
      exit(EXIT_FAILURE);
    }

  //
  //
  //
  if (rand_bits > 64)
    {
      fprintf(stderr, "Rand bits must be [0,64]\n");
      exit(EXIT_FAILURE);
    }

  //
  // get the physical device's memory props
  //
  VkPhysicalDeviceMemoryProperties phy_device_mem_props;

  vkGetPhysicalDeviceMemoryProperties(phy_device, &phy_device_mem_props);

  //
  // get queue properties
  //
  uint32_t qfp_count;

  vkGetPhysicalDeviceQueueFamilyProperties(phy_device, &qfp_count, NULL);

  VkQueueFamilyProperties qfp[qfp_count];

  vkGetPhysicalDeviceQueueFamilyProperties(phy_device, &qfp_count, qfp);

  //
  // HotSort only uses a single compute queue
  //
  float const qci_priorities[] = { 1.0f };

  VkDeviceQueueCreateInfo const qci = {

    .sType            = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
    .pNext            = NULL,
    .flags            = 0,
    .queueFamilyIndex = 0,
    .queueCount       = 1,
    .pQueuePriorities = qci_priorities
  };

  //
  // probe HotSort device requirements for this target
  //
  struct hotsort_vk_target_requirements hs_tr = { 0 };

  hotsort_vk_target_get_requirements(hs_target, &hs_tr); // returns false

  //
  // populate accumulated device requirements
  //
  char const *             ext_names[hs_tr.ext_name_count];
  VkPhysicalDeviceFeatures pdf = { false };

  //
  // populate HotSort device requirements
  //
  hs_tr.ext_names = ext_names;
  hs_tr.pdf       = &pdf;

  if (hotsort_vk_target_get_requirements(hs_target, &hs_tr) != true)
    {
      fprintf(stderr,
              "\"%s\", line %u: "
              "hotsort_vk_target_get_requirements(hs_target, &hs_tr) != true",
              __FILE__,
              __LINE__);
      exit(EXIT_FAILURE);
    }

  //
  // create VkDevice
  //
  VkDeviceCreateInfo const device_info = {

    .sType                   = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
    .pNext                   = NULL,
    .flags                   = 0,
    .queueCreateInfoCount    = 1,
    .pQueueCreateInfos       = &qci,
    .enabledLayerCount       = 0,
    .ppEnabledLayerNames     = NULL,
    .enabledExtensionCount   = hs_tr.ext_name_count,
    .ppEnabledExtensionNames = ext_names,
    .pEnabledFeatures        = &pdf
  };

  VkDevice device;

  vk(CreateDevice(phy_device, &device_info, NULL, &device));

  //
  // get a queue
  //
  VkQueue queue;

  vkGetDeviceQueue(device, 0, 0, &queue);

  //
  // get the pipeline cache
  //
  VkPipelineCache pc;

  vk_pipeline_cache_create(device, NULL, VK_PIPELINE_CACHE_PREFIX_STRING "vk_cache", &pc);

  //
  // create a descriptor set pool
  //
  VkDescriptorPoolSize const dps[] = { { .type            = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                                         .descriptorCount = 2 } };

  VkDescriptorPoolCreateInfo const dpci = {
    .sType         = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
    .pNext         = NULL,
    .flags         = 0,  // VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT,
    .maxSets       = 1,
    .poolSizeCount = ARRAY_LENGTH_MACRO(dps),
    .pPoolSizes    = dps
  };

  VkDescriptorPool dp;

  vk(CreateDescriptorPool(device,
                          &dpci,
                          NULL,  // allocator
                          &dp));

  //
  // create descriptor set layout
  //
  VkDescriptorSetLayoutCreateInfo const dscli = {
    .sType        = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
    .pNext        = NULL,
    .flags        = 0,
    .bindingCount = 2,  // 0:vout[], 1:vin[]
    .pBindings =
      (VkDescriptorSetLayoutBinding[]){

        { .binding            = 0,  // vout
          .descriptorType     = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
          .descriptorCount    = 1,
          .stageFlags         = VK_SHADER_STAGE_COMPUTE_BIT,
          .pImmutableSamplers = NULL },
        { .binding            = 1,  // vin
          .descriptorType     = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
          .descriptorCount    = 1,
          .stageFlags         = VK_SHADER_STAGE_COMPUTE_BIT,
          .pImmutableSamplers = NULL } }
  };

  VkDescriptorSetLayout dsl;

  vk(CreateDescriptorSetLayout(device,
                               &dscli,
                               NULL,  // allocator
                               &dsl));

  //
  // create pipeline layout
  //
  VkPipelineLayoutCreateInfo const plci = {

    .sType                  = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
    .pNext                  = NULL,
    .flags                  = 0,
    .setLayoutCount         = 1,
    .pSetLayouts            = &dsl,
    .pushConstantRangeCount = 1,
    .pPushConstantRanges =
      (VkPushConstantRange[]){

        { .stageFlags = HOTSORT_VK_PUSH_CONSTANT_RANGE_STAGE_FLAGS,
          .offset     = HOTSORT_VK_PUSH_CONSTANT_RANGE_OFFSET,
          .size       = HOTSORT_VK_PUSH_CONSTANT_RANGE_SIZE } }
  };

  VkPipelineLayout pl;

  vk(CreatePipelineLayout(device,
                          &plci,
                          NULL,  // allocator
                          &pl));

  //
  // create a descriptor set
  //
  VkDescriptorSetAllocateInfo const dsai = {

    .sType              = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
    .pNext              = NULL,
    .descriptorPool     = dp,
    .descriptorSetCount = 1,
    .pSetLayouts        = &dsl
  };

  VkDescriptorSet ds;

  vk(AllocateDescriptorSets(device, &dsai, &ds));

  struct hotsort_vk * const hs = hotsort_vk_create(device, NULL, pc, pl, hs_target);
  //
  // create a command pool for this thread
  //
  VkCommandPoolCreateInfo const cmd_pool_info = {
    .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
    .pNext = NULL,
    .flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT | VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,
    .queueFamilyIndex = 0,
  };

  VkCommandPool cmd_pool;

  vk(CreateCommandPool(device, &cmd_pool_info, NULL, &cmd_pool));

  //
  // create a query pool for benchmarking
  //
  bool const is_vk_timestamp_supported = phy_device_props.limits.timestampComputeAndGraphics;

  float const vk_timestamp_period =
    is_vk_timestamp_supported ? phy_device_props.limits.timestampPeriod : 1.0f;

#define QUERY_POOL_QUERY_COUNT 4

  static VkQueryPoolCreateInfo const query_pool_info = {

    .sType              = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO,
    .pNext              = NULL,
    .flags              = 0,
    .queryType          = VK_QUERY_TYPE_TIMESTAMP,
    .queryCount         = QUERY_POOL_QUERY_COUNT,
    .pipelineStatistics = 0
  };

  VkQueryPool query_pool;

  if (is_vk_timestamp_supported)
    {
      vk(CreateQueryPool(device, &query_pool_info, NULL, &query_pool));
    }

  //
  // create two big buffers -- buffer_out_count is always the largest
  //
  uint32_t slabs_in, buffer_in_count, buffer_out_count;

  hotsort_vk_pad(hs, count_hi, &slabs_in, &buffer_in_count, &buffer_out_count);

  size_t const buffer_out_size = buffer_out_count * key_val_words * sizeof(uint32_t);

  VkBufferCreateInfo bci = {

    .sType                 = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
    .pNext                 = NULL,
    .flags                 = 0,
    .size                  = buffer_out_size,
    .usage                 = 0,
    .sharingMode           = VK_SHARING_MODE_EXCLUSIVE,
    .queueFamilyIndexCount = 0,
    .pQueueFamilyIndices   = NULL
  };

  VkBuffer vin, vout, sorted, rand;

  bci.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT |  //
              VK_BUFFER_USAGE_TRANSFER_SRC_BIT |    // SRC in case buffer size is 1
              VK_BUFFER_USAGE_TRANSFER_DST_BIT;

  vk(CreateBuffer(device, &bci, NULL, &vin));

  vk(CreateBuffer(device, &bci, NULL, &sorted));

  bci.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT |  //
              VK_BUFFER_USAGE_TRANSFER_SRC_BIT |    //
              VK_BUFFER_USAGE_TRANSFER_DST_BIT;

  vk(CreateBuffer(device, &bci, NULL, &vout));

  bci.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT |  //
              VK_BUFFER_USAGE_TRANSFER_SRC_BIT;

  vk(CreateBuffer(device, &bci, NULL, &rand));

  //
  // get memory requirements for one of the buffers
  //
  VkMemoryRequirements mr_vin, mr_vout, mr_sorted, mr_rand;

  vkGetBufferMemoryRequirements(device, vin, &mr_vin);
  vkGetBufferMemoryRequirements(device, vout, &mr_vout);

  vkGetBufferMemoryRequirements(device, sorted, &mr_sorted);
  vkGetBufferMemoryRequirements(device, rand, &mr_rand);

  //
  // allocate memory for the buffers
  //
  // for simplicity, all buffers are the same size
  //
  // vin and vout have the same usage
  //
  VkMemoryAllocateInfo const mai_vin = {

    .sType           = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
    .pNext           = NULL,
    .allocationSize  = mr_vin.size,
    .memoryTypeIndex = vk_find_mem_type_idx(&phy_device_mem_props,
                                            mr_vin.memoryTypeBits,
                                            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT)
  };

  VkMemoryAllocateInfo const mai_vout = {

    .sType           = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
    .pNext           = NULL,
    .allocationSize  = mr_vout.size,
    .memoryTypeIndex = vk_find_mem_type_idx(&phy_device_mem_props,
                                            mr_vout.memoryTypeBits,
                                            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT)
  };

  VkMemoryAllocateInfo const mai_sorted = {

    .sType           = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
    .pNext           = NULL,
    .allocationSize  = mr_sorted.size,
    .memoryTypeIndex = vk_find_mem_type_idx(
      &phy_device_mem_props,
      mr_sorted.memoryTypeBits,
      VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)
  };

  VkMemoryAllocateInfo const mai_rand = {

    .sType           = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
    .pNext           = NULL,
    .allocationSize  = mr_rand.size,
    .memoryTypeIndex = vk_find_mem_type_idx(
      &phy_device_mem_props,
      mr_rand.memoryTypeBits,
      VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)
  };

  VkDeviceMemory mem_vin, mem_vout, mem_sorted, mem_rand;

  vk(AllocateMemory(device, &mai_vin, NULL, &mem_vin));

  vk(AllocateMemory(device, &mai_vout, NULL, &mem_vout));

  vk(AllocateMemory(device, &mai_sorted, NULL, &mem_sorted));

  vk(AllocateMemory(device, &mai_rand, NULL, &mem_rand));

  //
  // bind backing memory to the virtual allocations
  //
  vk(BindBufferMemory(device, vin, mem_vin, 0));
  vk(BindBufferMemory(device, vout, mem_vout, 0));

  vk(BindBufferMemory(device, sorted, mem_sorted, 0));
  vk(BindBufferMemory(device, rand, mem_rand, 0));

  //
  // map and fill the rand buffer with random values
  //
  void * rand_h   = malloc(buffer_out_size);
  void * sorted_h = malloc(buffer_out_size);

  hs_fill_rand(rand_h, buffer_out_count, key_val_words, rand_bits);

  void * rand_map;

  vk(MapMemory(device, mem_rand, 0, VK_WHOLE_SIZE, 0, &rand_map));

  memcpy(rand_map, rand_h, buffer_out_size);

  vkUnmapMemory(device, mem_rand);

  //
  // create a single command buffer for this thread
  //
  VkCommandBufferAllocateInfo const cmd_buffer_info = {
    .sType              = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
    .pNext              = NULL,
    .commandPool        = cmd_pool,
    .level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
    .commandBufferCount = 1
  };

  VkCommandBuffer cb;

  vk(AllocateCommandBuffers(device, &cmd_buffer_info, &cb));

  //
  //
  //
  static VkCommandBufferBeginInfo const cb_begin_info = {
    .sType            = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
    .pNext            = NULL,
    .flags            = 0,  // VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
    .pInheritanceInfo = NULL
  };

  struct VkSubmitInfo const submit_info = {

    .sType                = VK_STRUCTURE_TYPE_SUBMIT_INFO,
    .pNext                = NULL,
    .waitSemaphoreCount   = 0,
    .pWaitSemaphores      = NULL,
    .pWaitDstStageMask    = NULL,
    .commandBufferCount   = 1,
    .pCommandBuffers      = &cb,
    .signalSemaphoreCount = 0,
    .pSignalSemaphores    = NULL
  };

  //
  // update the descriptor set
  //
  VkDescriptorBufferInfo const dbi[] = {

    { .buffer = vout, .offset = 0, .range = VK_WHOLE_SIZE },
    { .buffer = vin, .offset = 0, .range = VK_WHOLE_SIZE }
  };

  VkWriteDescriptorSet const wds[] = {

    { .sType            = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
      .pNext            = NULL,
      .dstSet           = ds,
      .dstBinding       = 0,
      .dstArrayElement  = 0,
      .descriptorCount  = 2,
      .descriptorType   = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
      .pImageInfo       = NULL,
      .pBufferInfo      = dbi,
      .pTexelBufferView = NULL }
  };

  vkUpdateDescriptorSets(device, ARRAY_LENGTH_MACRO(wds), wds, 0, NULL);

  //
  // buffer offsets
  //
  struct hotsort_vk_ds_offsets const offsets = { .in = 0UL, .out = 0UL };

  //
  // labels
  //
  fprintf(stderr,
          "Device, "
          "Driver, "
          "Type, "
          "Slab/Linear, "
          "Verified?, "
          "Keys, "
          "Keys Padded In, "
          "Keys Padded Out, "
          "CPU, "
          "Algorithm, "
          "CPU Msecs, "
          "CPU Mkeys/s, "
          "GPU, "
          "Trials, "
          "Avg. Msecs, "
          "Min Msecs, "
          "Max Msecs, "
          "Avg. Mkeys/s, "
          "Max. Mkeys/s\n");

  //
  // accumulate verifications
  //
  bool all_verified = true;

  //
  // test a range
  //
  for (uint32_t count = count_lo; count <= count_hi; count += count_step)
    {
      //
      // size the vin and vout arrays
      //
      uint32_t slabs_in, count_padded_in, count_padded_out;

      hotsort_vk_pad(hs, count, &slabs_in, &count_padded_in, &count_padded_out);

      //
      // initialize vin with 'count' random keys
      //
      vkBeginCommandBuffer(cb, &cb_begin_info);

      VkBufferCopy const copy_rand = {

        .srcOffset = 0,
        .dstOffset = 0,
        .size      = count * key_val_words * sizeof(uint32_t)
      };

      vkCmdCopyBuffer(cb, rand, vin, 1, &copy_rand);

      vk(EndCommandBuffer(cb));

      vk(QueueSubmit(queue, 1, &submit_info,
                     VK_NULL_HANDLE));  // FIXME -- put a fence here

      // wait for queue to drain
      vk(QueueWaitIdle(queue));
      vk(ResetCommandBuffer(cb, 0));

      //
      // build the sorting command buffer
      //
      vkBeginCommandBuffer(cb, &cb_begin_info);

      //
      // reset the query pool
      //
      if (is_vk_timestamp_supported)
        {
          vkCmdResetQueryPool(cb, query_pool, 0, QUERY_POOL_QUERY_COUNT);
        }

      //
      // starting timestamp
      //
      if (is_vk_timestamp_supported)
        {
          vkCmdWriteTimestamp(cb, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, query_pool, 0);
        }

      //
      // bind the vin/vout buffers early
      //
      vkCmdBindDescriptorSets(cb, VK_PIPELINE_BIND_POINT_COMPUTE, pl, 0, 1, &ds, 0, NULL);

      //
      // append sorting commands
      //
      hotsort_vk_sort(cb, hs, &offsets, count, count_padded_in, count_padded_out, linearize);

      //
      // end timestamp
      //
      if (is_vk_timestamp_supported)
        {
          vkCmdWriteTimestamp(cb, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, query_pool, 1);
        }

      //
      // end the command buffer
      //
      vk(EndCommandBuffer(cb));

      //
      // measure the min/max/avg execution time
      //
      uint64_t elapsed_ns_min;
      uint64_t elapsed_ns_max;
      uint64_t elapsed_ns_sum;

      for (uint32_t ii = 0; ii < warmup + loops; ii++)
        {
          if (ii == warmup)
            {
              elapsed_ns_min = UINT64_MAX;
              elapsed_ns_max = 0;
              elapsed_ns_sum = 0;
            }

          // if the device doesn't support timestamps then measure wall-time
          uint64_t timestamps[2];

          if (!is_vk_timestamp_supported)
            {
              struct timespec ts;

              timespec_get(&ts, TIME_UTC);

              timestamps[0] = ts.tv_sec * 1000000000L + ts.tv_nsec;
            }

          // submit!
          vk(QueueSubmit(queue, 1, &submit_info, VK_NULL_HANDLE));

          // wait for queue to drain
          vk(QueueWaitIdle(queue));

          if (!is_vk_timestamp_supported)
            {
              struct timespec ts;

              timespec_get(&ts, TIME_UTC);

              timestamps[1] = ts.tv_sec * 1000000000L + ts.tv_nsec;
            }
          else
            {
              vk(GetQueryPoolResults(device,
                                     query_pool,
                                     0,
                                     ARRAY_LENGTH_MACRO(timestamps),
                                     sizeof(timestamps),
                                     timestamps,
                                     sizeof(timestamps[0]),
                                     VK_QUERY_RESULT_64_BIT | VK_QUERY_RESULT_WAIT_BIT));
            }

          uint64_t const t = timestamps[1] - timestamps[0];

          elapsed_ns_min = MIN_MACRO(uint64_t, elapsed_ns_min, t);
          elapsed_ns_max = MAX_MACRO(uint64_t, elapsed_ns_max, t);
          elapsed_ns_sum += t;
        }

      vk(ResetCommandBuffer(cb, 0));

      //
      // copy the results back and, optionally, verify them
      //
      char const * cpu_algo = NULL;
      double       cpu_ns   = 0.0;
      bool         verified = true;

      if (verify)
        {
          size_t const size_padded_in = count_padded_in * key_val_words * sizeof(uint32_t);

          vkBeginCommandBuffer(cb, &cb_begin_info);

          VkBufferCopy const copy_vout = { .srcOffset = 0, .dstOffset = 0, .size = size_padded_in };

          vkCmdCopyBuffer(cb, (count == 1) ? vin : vout, sorted, 1, &copy_vout);

          vk(EndCommandBuffer(cb));

          vk(QueueSubmit(queue, 1, &submit_info,
                         VK_NULL_HANDLE));  // FIXME -- put a fence here

          // wait for queue to drain
          vk(QueueWaitIdle(queue));
          vk(ResetCommandBuffer(cb, 0));

          size_t const size_sorted_h = count * key_val_words * sizeof(uint32_t);

          // run the cpu_algo N times and measure last
          uint32_t cpu_loops = HS_BENCH_LOOPS_CPU;
          do
            {
              // copy random data
              memcpy(sorted_h, rand_h, size_sorted_h);
              // fill
              memset((uint8_t *)sorted_h + size_sorted_h, -1, size_padded_in - size_sorted_h);
              // sort
              cpu_algo = hs_cpu_sort(sorted_h, key_val_words, count_padded_in, &cpu_ns);
          } while (--cpu_loops > 0);

          void * sorted_map;

          vk(MapMemory(device, mem_sorted, 0, VK_WHOLE_SIZE, 0, &sorted_map));

          if (!linearize)
            {
              hs_transpose_slabs(key_val_words,
                                 1u << hs_target->config.slab.width_log2,
                                 hs_target->config.slab.height,
                                 sorted_map,
                                 count_padded_in);
            }

          // verify
          verified = memcmp(sorted_h, sorted_map, size_padded_in) == 0;

#if !defined(NDEBUG) && defined(HS_VK_DUMP_SLABS)
          if (!verified)
            {
              if (key_val_words == 1)
                {
                  hs_debug_u32(1u << hs_target->config.slab.width_log2,
                               hs_target->config.slab.height,
                               sorted_h,
                               count);

                  hs_debug_u32(1u << hs_target->config.slab.width_log2,
                               hs_target->config.slab.height,
                               sorted_map,
                               count);
                }
              else  // ulong
                {
                  hs_debug_u64(1u << hs_target->config.slab.width_log2,
                               hs_target->config.slab.height,
                               sorted_h,
                               count);

                  hs_debug_u64(1u << hs_target->config.slab.width_log2,
                               hs_target->config.slab.height,
                               sorted_map,
                               count);
                }
            }
#endif

          vkUnmapMemory(device, mem_sorted);
        }

      //
      // any verification failures?
      //
      all_verified = all_verified && verified;

      //
      // REPORT
      //
      double const elapsed_ns_min_f64 = (double)elapsed_ns_min;
      double const elapsed_ns_max_f64 = (double)elapsed_ns_max;
      double const elapsed_ns_sum_f64 = (double)elapsed_ns_sum;

      fprintf(
        stderr,
        "%s, %u.%u.%u.%u, %s, %s, %s, %8u, %8u, %8u, CPU, %s, %9.2f, %6.2f, GPU, %9u, %7.3f, %7.3f, %7.3f, %7.2f, %7.2f\n",
        phy_device_props.deviceName,
        (phy_device_props.driverVersion >> 24) & 0xFF,
        (phy_device_props.driverVersion >> 16) & 0xFF,
        (phy_device_props.driverVersion >> 8) & 0xFF,
        (phy_device_props.driverVersion) & 0xFF,
        (key_val_words == 1) ? "uint" : "ulong",
        linearize ? "linear" : "slab",
        verify ? (verified ? "  OK  " : "*FAIL*") : "UNVERIFIED",
        count,
        count_padded_in,
        count_padded_out,
        // CPU
        verify ? cpu_algo : "UNVERIFIED",
        verify ? (cpu_ns / 1000000.0) : 0.0,       // milliseconds
        verify ? (1000.0 * count / cpu_ns) : 0.0,  // mkeys / sec
        // GPU
        loops,
        (vk_timestamp_period * elapsed_ns_sum_f64) / 1e6 / loops,             // avg msecs
        (vk_timestamp_period * elapsed_ns_min_f64) / 1e6,                     // min msecs
        (vk_timestamp_period * elapsed_ns_max_f64) / 1e6,                     // max msecs
        1000.0 * count * loops / (vk_timestamp_period * elapsed_ns_sum_f64),  // mkeys / sec - avg
        1000.0 * count / (vk_timestamp_period * elapsed_ns_min_f64));         // mkeys / sec - max
    }

  //
  // cleanup
  //

  vk(ResetDescriptorPool(device, dp, 0));  // implicitly frees descriptor sets

  vkDestroyDescriptorPool(device, dp, NULL);

  vkDestroyDescriptorSetLayout(device, dsl, NULL);

  vkDestroyPipelineLayout(device, pl, NULL);

  // release shared HotSort state
  hotsort_vk_release(device, NULL, hs);

  // destroy the vin/vout buffers (before device memory)
  vkDestroyBuffer(device, vin, NULL);
  vkDestroyBuffer(device, vout, NULL);
  vkDestroyBuffer(device, sorted, NULL);
  vkDestroyBuffer(device, rand, NULL);

  // free device memory
  vkFreeMemory(device, mem_vin, NULL);
  vkFreeMemory(device, mem_vout, NULL);
  vkFreeMemory(device, mem_sorted, NULL);
  vkFreeMemory(device, mem_rand, NULL);

  // free host memory
  free(rand_h);
  free(sorted_h);

  // destroy query pool
  if (is_vk_timestamp_supported)
    {
      vkDestroyQueryPool(device, query_pool, NULL);
    }

  // destroy remaining...
  vkFreeCommandBuffers(device, cmd_pool, 1, &cb);
  vkDestroyCommandPool(device, cmd_pool, NULL);

  //
  // save the pipeline cache
  //
  vk_pipeline_cache_destroy(device, NULL, VK_PIPELINE_CACHE_PREFIX_STRING "vk_cache", pc);

  vkDestroyDevice(device, NULL);

  vkDestroyInstance(instance, NULL);

  return all_verified ? EXIT_SUCCESS : EXIT_FAILURE;
}

//
//
//
