// Copyright 2021 The Fuchsia Authors. All rights reserved.
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

//
//
//
#include "common/macros.h"
#include "common/vk/assert.h"
#include "common/vk/barrier.h"
#include "common/vk/cache.h"
#include "common/vk/find_mem_type_idx.h"

//
//
//
#include "radix_sort/platforms/vk/radix_sort_vk.h"  // public

//
// This test is a friend of the radix sort library and uses private includes.
//
#include "radix_sort/platforms/vk/radix_sort_vk_ext.h"  // private
#include "radix_sort/platforms/vk/shaders/push.h"       // private

//
// Is the target archive(s) linkable or loadable?
//
#ifdef RS_TARGET_ARCHIVE_LINKABLE

#include "amd_gcn3_u32_rodata.h"
#include "amd_gcn3_u64_rodata.h"
#include "arm_bifrost4_u32_rodata.h"
#include "arm_bifrost8_u32_rodata.h"
#include "intel_gen8_u32_rodata.h"
#include "intel_gen8_u64_rodata.h"
#include "nvidia_sm35_u32_rodata.h"
#include "nvidia_sm35_u64_rodata.h"

#endif

//
// Define a platform-specific prefix
//
#ifdef __Fuchsia__
#define VK_PIPELINE_CACHE_PREFIX_STRING "/cache/."
#else
#define VK_PIPELINE_CACHE_PREFIX_STRING "."
#endif

//
// Define or uncomment to enable debug printing.
//
// #define RS_DEBUG_DUMP

//
// Define or uncomment to enable "split" timestamps.
//
// #define RS_ENABLE_EXT_TIMESTAMPS

//
//
//
// clang-format off
#define RS_BENCH_LOOPS      1
#define RS_BENCH_WARMUP     0
#define RS_BENCH_LOOPS_CPU  1
// clang-format on

//
//
//
char const *
cpu_sort_u32(uint32_t * a, uint32_t count, double * cpu_ns);
char const *
cpu_sort_u64(uint64_t * a, uint32_t count, double * cpu_ns);

//
//
//
static char const *
cpu_sort(void * sorted_h, uint32_t const rs_dwords, uint32_t const count, double * const cpu_ns)
{
  char const * algo;

  if (rs_dwords == 1)
    {
      algo = cpu_sort_u32(sorted_h, count, cpu_ns);
    }
  else
    {
      algo = cpu_sort_u64(sorted_h, count, cpu_ns);
    }

  return algo;
}

//
//
//
static uint32_t
rs_rand_u32()
{
#if 1
  static uint32_t seed = 0xDEADBEEF;

  // Numerical Recipes
  seed = seed * 1664525 + 1013904223;

  return seed;
#else
  return 0x01020304;
#endif
}

//
//
//
static void
rs_fill_rand(uint32_t * vin_h, uint32_t const count, uint32_t const dwords)
{
  for (uint32_t ii = 0; ii < count * dwords; ii++)
    {
      vin_h[ii] = rs_rand_u32();
    }
}

//
// Loadable vs. linkable target archives
//
//   - Loadable expects a path to a target archive ".ar" file
//   - Linkable expects the target name of the archive
//
#ifndef RS_TARGET_ARCHIVE_LINKABLE

//
// Load the target binary.
//
// Must be freed by caller.
//
static struct radix_sort_vk_target const *
rs_load_target(char const * filename)
{
  FILE * file = fopen(filename, "r");

  if (file == NULL)
    {
      fprintf(stderr, "Error: Can't open target filename \"%s\"\n", filename);
      return NULL;
    }

  if (fseek(file, 0, SEEK_END) != 0)
    {
      fprintf(stderr, "Error: Can't seek target filename \"%s\"\n", filename);
      fclose(file);
      return NULL;
    }

  long int const file_size = ftell(file);

  if (file_size == -1L)
    {
      fprintf(stderr, "Error: Can't determine size of target filename \"%s\"\n", filename);
      fclose(file);
      return NULL;
    }

  if (fseek(file, 0, SEEK_SET) != 0)
    {
      fprintf(stderr, "Error: Can't seek target filename \"%s\"\n", filename);
      fclose(file);
      return NULL;
    }

  struct radix_sort_vk_target * target = malloc(file_size);

  if (fread(target, 1, file_size, file) != (size_t)file_size)
    {
      fprintf(stderr, "Error: Can't read target filename \"%s\"\n", filename);
      fclose(file);
      free(target);
      return NULL;
    }

  fclose(file);

  return target;
}

#else

// clang-format off
struct rs_name_target
{
  char const *                           name;
  union
  {
    struct target_archive_header const * header;
    struct radix_sort_vk_target const *  target;
  };
};

static struct rs_name_target const rs_named_targets[] = {
  { .name = "amd_gcn3_u32",     .header = amd_gcn3_u32_rodata     },
  { .name = "amd_gcn3_u64",     .header = amd_gcn3_u64_rodata     },
  { .name = "arm_bifrost4_u32", .header = arm_bifrost4_u32_rodata },
  { .name = "arm_bifrost8_u32", .header = arm_bifrost8_u32_rodata },
  { .name = "intel_gen8_u32",   .header = intel_gen8_u32_rodata   },
  { .name = "intel_gen8_u64",   .header = intel_gen8_u64_rodata   },
  { .name = "nvidia_sm35_u32",  .header = nvidia_sm35_u32_rodata  },
  { .name = "nvidia_sm35_u64",  .header = nvidia_sm35_u64_rodata  },
};
// clang-format on

//
// No matching target name
//
static void
rs_list_targets()
{
  fprintf(stderr, "Supported target names:\n");

  for (uint32_t ii = 0; ii < ARRAY_LENGTH_MACRO(rs_named_targets); ii++)
    {
      fprintf(stderr, "  %s\n", rs_named_targets[ii].name);
    }
}

//
// Find matching target
//
static struct radix_sort_vk_target const *
rs_load_target(char const * target_name)
{
  for (uint32_t ii = 0; ii < ARRAY_LENGTH_MACRO(rs_named_targets); ii++)
    {
      if (strcmp(target_name, rs_named_targets[ii].name) == 0)
        {
          return rs_named_targets[ii].target;
        }
    }

  return NULL;
}

#endif

//
// Debug dump routines
//
#ifdef RS_DEBUG_DUMP

static void
rs_debug_dump_256(char const * const     label,
                  uint32_t const * const base,
                  uint32_t const         first,
                  uint32_t const         last,
                  uint32_t const         row_len)
{
  for (uint32_t ii = first; ii < last; ii++)
    {
      fprintf(stdout, label, ii);

      for (uint32_t jj = 0; jj < (RS_RADIX_SIZE / row_len); jj++)
        {
          for (uint32_t kk = 0; kk < row_len; kk++)
            {
              fprintf(stdout, "%8X ", base[ii * RS_RADIX_SIZE + jj * row_len + kk]);
            }

          fprintf(stdout, "\n");
        }
    }
}

static void
rs_debug_dump_histograms(uint32_t const * const histograms,
                         uint32_t const         keyval_bytes,
                         uint32_t const         passes,
                         uint32_t const         row_len)
{
  rs_debug_dump_256("HISTOGRAM %3u ---------------\n",
                    histograms,
                    keyval_bytes - passes,
                    keyval_bytes,
                    row_len);
}

static void
rs_debug_dump_keyvals_u32(uint32_t const * const sort,
                          uint32_t const * const gold,
                          uint32_t const         row_len,
                          uint32_t const         count)
{
  uint32_t ii = 0;

  while (ii < count)
    {
      const bool is_equal = (sort[ii] == gold[ii]);

      fprintf(stdout, "%08" PRIX32 "%c", sort[ii], is_equal ? ' ' : '|');

      if (++ii % row_len == 0)
        {
          fprintf(stdout, "\n");
        }
    }

  fprintf(stdout, "\n");
}

static void
rs_debug_dump_keyvals_u64(uint64_t const * const sort,
                          uint64_t const * const gold,
                          uint32_t const         row_len,
                          uint32_t const         count)
{
  uint32_t ii = 0;

  while (ii < count)
    {
      const bool is_equal = (sort[ii] == gold[ii]);

      fprintf(stdout, "%016" PRIX64 "%c", sort[ii], is_equal ? ' ' : '|');

      if (++ii % row_len == 0)
        {
          fprintf(stdout, "\n");
        }
    }

  fprintf(stdout, "\n");
}

static void
rs_debug_dump_indirect(struct rs_indirect_info const * info)
{
  fprintf(stdout,
          "pad.fill           = { .block_offset               = %10u\n"
          "                       .dword_offset_min           = %10u\n"
          "                       .dword_offset_max_minus_min = %10u  }\n",
          info->pad.block_offset,
          info->pad.dword_offset_min,
          info->pad.dword_offset_max_minus_min);

  fprintf(stdout,
          "pad.zero           = { .block_offset               = %10u\n"
          "                       .dword_offset_min           = %10u\n"
          "                       .dword_offset_max_minus_min = %10u  }\n",
          info->zero.block_offset,
          info->zero.dword_offset_min,
          info->zero.dword_offset_max_minus_min);

  fprintf(stdout,
          "dispatch.pad       = { %10u, %10u, %10u }\n",
          info->dispatch.pad.x,
          info->dispatch.pad.y,
          info->dispatch.pad.z);

  fprintf(stdout,
          "dispatch.zero      = { %10u, %10u, %10u }\n",
          info->dispatch.zero.x,
          info->dispatch.zero.y,
          info->dispatch.zero.z);

  fprintf(stdout,
          "dispatch.histogram = { %10u, %10u, %10u }\n",
          info->dispatch.histogram.x,
          info->dispatch.histogram.y,
          info->dispatch.histogram.z);

  fprintf(stdout,
          "dispatch.scatter   = { %10u, %10u, %10u }\n",
          info->dispatch.scatter.x,
          info->dispatch.scatter.y,
          info->dispatch.scatter.z);
}

#endif

//
//
//
static void
rs_usage(char const * const argv[])
{
  fprintf(
    stderr,
    "\n"
    "Usage: %s "
    "<vendorID>:<deviceID> "
#if defined(__Fuchsia__) && !defined(RS_TARGET_ARCHIVE_LINKABLE)
    "<target file> "
#else
    "<target name> "
#endif
    "<\"direct\"|\"indirect\"> "
    "[count lo "
    "[count hi "
    "[count step "
    "[iterations "
    "[warmup "
    "[validate?] ] ] ] ] ]\n\n"

    "  <vendorID>:<deviceID> : Execute on a specific Vulkan physical device.\n"
#if defined(__Fuchsia__) && !defined(RS_TARGET_ARCHIVE_LINKABLE)
    "  <target file>         : Name of Radix Sort target file: \"pkg/data/targets/<vendor name>_<arch name>_<u32 or u64>.ar\".\n"
#else
    "  <target name>         : Name of Radix Sort target: <vendor name>_<arch name>_<u32 or u64>.\n"
#endif
    "  <\"direct\"|\"indirect\"> : Direct or indirect radix sort.\n"
    "  <count lo>            : Initial number of keyvals.\n"
    "  <count hi>            : Final number of keyvals.\n"
    "  <count step>          : Keyval step size.\n"
    "  <iterations>          : Number of times each step is executed in the benchmark.\n"
    "  <warmup>              : Number of times each step is executed before starting the benchmark.\n"
    "  <validate?>           : If 0 then skip validating the device sorted results against a CPU sorting algorithm.\n\n",
    argv[0]);

#ifdef RS_TARGET_ARCHIVE_LINKABLE
  rs_list_targets();
#endif
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
  uint32_t vendor_id = 0, device_id = 0;

  if (argc > 1)
    {
      vendor_id = (uint32_t)strtoul(argv[1], NULL, 16);  // returns 0 on error

      char const * colon = strchr(argv[1], ':');

      if (colon != NULL)
        {
          device_id = (uint32_t)strtoul(strchr(argv[1], ':') + 1, NULL, 16);  // returns 0 on error
        }
    }

  //
  // create a Vulkan 1.2 instance
  //
  VkApplicationInfo const app_info = {

    .sType              = VK_STRUCTURE_TYPE_APPLICATION_INFO,
    .pNext              = NULL,
    .pApplicationName   = "Fuchsia RadixSort/VK Bench",
    .applicationVersion = 0,
    .pEngineName        = "Fuchsia RadixSort/VK",
    .engineVersion      = 0,
    .apiVersion         = VK_API_VERSION_1_2
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
  uint32_t pd_count;

  vk(EnumeratePhysicalDevices(instance, &pd_count, NULL));

  VkPhysicalDevice * pds = malloc(pd_count * sizeof(*pds));

  vk(EnumeratePhysicalDevices(instance, &pd_count, pds));

  VkPhysicalDevice           pd = VK_NULL_HANDLE;
  VkPhysicalDeviceProperties pdp;

  for (uint32_t ii = 0; ii < pd_count; ii++)
    {
      VkPhysicalDeviceProperties tmp;

      vkGetPhysicalDeviceProperties(pds[ii], &tmp);

      bool const is_match = (tmp.vendorID == vendor_id) &&  //
                            (tmp.deviceID == device_id);

      fprintf(stdout,
              "%c %4X : %X : %s\n",
              is_match ? '*' : ' ',
              tmp.vendorID,
              tmp.deviceID,
              tmp.deviceName);

      if (is_match)
        {
          pd = pds[ii];
          memcpy(&pdp, &tmp, sizeof(tmp));
        }
    }

  fprintf(stdout, "\n");

  if (pd == VK_NULL_HANDLE)
    {
      fprintf(stderr, "Error: Device %4X:%X not found\n", vendor_id, device_id);

      rs_usage(argv);

      return EXIT_FAILURE;
    }

  free(pds);

  //
  // timestamp support is required
  //
  if (pdp.limits.timestampComputeAndGraphics != VK_TRUE)
    {
      fprintf(stderr, "Error: Vulkan `pdp.limits.timestampComputeAndGraphics != VK_TRUE`\n");
      exit(EXIT_FAILURE);
    }

  //
  // make sure there is a target filename
  //
  if (argc < 3)
    {
#if defined(__Fuchsia__) && !defined(RS_TARGET_ARCHIVE_LINKABLE)
      fprintf(stderr, "Error: Missing target filename\n");
#else
      fprintf(stderr, "Error: Missing target name\n");
#endif

      rs_usage(argv);

      exit(EXIT_FAILURE);
    }

  //
  // load the target
  //
  struct radix_sort_vk_target const * rs_target = rs_load_target(argv[2]);

  if (rs_target == NULL)
    {
      fprintf(stderr, "Error: Target \"%s\" not found\n", argv[2]);

      rs_usage(argv);

      exit(EXIT_FAILURE);
    }

  //
  // make sure there is a target filename
  //
  if (argc < 4)
    {
      fprintf(stderr, "Error: Missing \"direct\" or \"indirect\" dispatch mode\n");

      rs_usage(argv);

      exit(EXIT_FAILURE);
    }

  //
  // Direct or indirect radix sort dispatch?
  //
  bool is_direct;

  if (strcmp(argv[3], "direct") == 0)
    {
      is_direct = true;
    }
  else if (strcmp(argv[3], "indirect") == 0)
    {
      is_direct = false;
    }
  else
    {
      fprintf(stderr, "Error: Specify either \"direct\" or \"indirect\"\n");

      rs_usage(argv);

      exit(EXIT_FAILURE);
    }

  //
  // get rest of command line
  //
  // clang-format on
  uint32_t const count_lo   = (argc <= 4) ? 1024 : (uint32_t)strtoul(argv[4], NULL, 0);
  uint32_t const count_hi   = (argc <= 5) ? count_lo : (uint32_t)strtoul(argv[5], NULL, 0);
  uint32_t const count_step = (argc <= 6) ? count_lo : (uint32_t)strtoul(argv[6], NULL, 0);
  uint32_t const loops      = (argc <= 7) ? RS_BENCH_LOOPS : (uint32_t)strtoul(argv[7], NULL, 0);
  uint32_t const warmup     = (argc <= 8) ? RS_BENCH_WARMUP : (uint32_t)strtoul(argv[8], NULL, 0);
  bool const     verify     = (argc <= 9) ? true : strtoul(argv[9], NULL, 0) != 0;
  // clang-format on

  //
  // arg validation
  //
  if (count_lo == 0)
    {
      fprintf(stderr, "Error: Keyval count must be >= 1\n");
      exit(EXIT_FAILURE);
    }

  if (count_lo > count_hi)
    {
      fprintf(stderr, "Error: count_lo > count_hi\n");
      exit(EXIT_FAILURE);
    }

  if (loops == 0)
    {
      fprintf(stderr, "Error: Loops must be non-zero\n");
      exit(EXIT_FAILURE);
    }

  //
  // get the physical device's memory props
  //
  VkPhysicalDeviceMemoryProperties pdmp;

  vkGetPhysicalDeviceMemoryProperties(pd, &pdmp);

  //
  // get queue properties
  //
  uint32_t qfp_count;

  vkGetPhysicalDeviceQueueFamilyProperties(pd, &qfp_count, NULL);

  VkQueueFamilyProperties qfp[qfp_count];

  vkGetPhysicalDeviceQueueFamilyProperties(pd, &qfp_count, qfp);

  //
  // one compute queue
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
  // probe Radix Sort device extension requirements for this target
  //
  struct radix_sort_vk_target_requirements rs_tr = { 0 };

  radix_sort_vk_target_get_requirements(rs_target, &rs_tr);  // returns false

  //
  // feature structures chain
  //
  VkPhysicalDeviceVulkan12Features pdf12 = {
    .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES,
  };

  VkPhysicalDeviceVulkan11Features pdf11 = {
    .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_FEATURES,
    .pNext = &pdf12
  };

  VkPhysicalDeviceFeatures2 pdf2 = { .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2,
                                     .pNext = &pdf11 };

  //
  // populate Radix Sort device requirements
  //
  char const * ext_names[rs_tr.ext_name_count];

  rs_tr.ext_names = ext_names;
  rs_tr.pdf       = &pdf2.features;
  rs_tr.pdf11     = &pdf11;
  rs_tr.pdf12     = &pdf12;

  if (radix_sort_vk_target_get_requirements(rs_target, &rs_tr) != true)
    {
      fprintf(stderr, "Error: radix_sort_vk_target_get_requirements(...) != true\n");
      exit(EXIT_FAILURE);
    }

  //
  // create VkDevice
  //
  VkDeviceCreateInfo const device_info = {

    .sType                   = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
    .pNext                   = &pdf2,
    .flags                   = 0,
    .queueCreateInfoCount    = 1,
    .pQueueCreateInfos       = &qci,
    .enabledLayerCount       = 0,
    .ppEnabledLayerNames     = NULL,
    .enabledExtensionCount   = rs_tr.ext_name_count,
    .ppEnabledExtensionNames = ext_names,
    .pEnabledFeatures        = NULL
  };

  VkDevice device;

  vk(CreateDevice(pd, &device_info, NULL, &device));

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
  // create Radix Sort instance
  //
  struct radix_sort_vk * const rs = radix_sort_vk_create(device, NULL, pc, rs_target);

  //
  // free the target archive
  //
#ifndef RS_TARGET_ARCHIVE_LINKABLE
  free((void *)rs_target);
#endif

  //
  // was the radix sort instance successfully created?
  //
  if (rs == NULL)
    {
      fprintf(stderr, "Failed to create Radix Sort target!\n");
      exit(EXIT_FAILURE);
    }

  //
  // create command pool
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
  float const vk_timestamp_period = pdp.limits.timestampPeriod;

  //
  // Number of timestamps is: 5 + (number of subpasses)
  //
  //   * direct   dispatch: 4 + subpass count
  //   * indirect dispatch: 5 + subpass count
  //
  // Indirect / 32-bit keyvals: 9
  // Indirect / 64-bit keyvals: 13
  //
#define QUERY_POOL_SIZE_MAX (1 + 4 + 8)

  VkQueryPoolCreateInfo const query_pool_info = {

    .sType              = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO,
    .pNext              = NULL,
    .flags              = 0,
    .queryType          = VK_QUERY_TYPE_TIMESTAMP,
    .queryCount         = QUERY_POOL_SIZE_MAX,
    .pipelineStatistics = 0
  };

  VkQueryPool query_pool;

  vk(CreateQueryPool(device, &query_pool_info, NULL, &query_pool));

  //
  // get target's memory requirements
  //
  struct radix_sort_vk_memory_requirements rs_mr;

  radix_sort_vk_get_memory_requirements(rs, count_hi, &rs_mr);

  uint32_t const keyval_bytes  = (uint32_t)rs_mr.keyval_size;
  uint32_t const keyval_dwords = keyval_bytes / 4;

  //
  // create buffers:
  //
  //   * rand buffer
  //
  //   * keyval buffer x 2
  //   * mappable keyval buffer
  //
  //   * internal buffer
  //   * mappable internal buffer
  //
  //   * indirect buffer
  //   * mappable indirect buffer
  //
  //   * count buffer (1 dword)
  //
  VkBufferCreateInfo bci = {

    .sType                 = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
    .pNext                 = NULL,
    .flags                 = 0,
    .sharingMode           = VK_SHARING_MODE_EXCLUSIVE,
    .queueFamilyIndexCount = 0,
    .pQueueFamilyIndices   = NULL
    // .size
    // .usage
  };

  struct
  {
    VkBuffer rand;
    VkBuffer keyvals_even;
    VkBuffer keyvals_odd;
    VkBuffer map_keyvals;
    VkBuffer internal;
    VkBuffer map_internal;
    VkBuffer indirect;
    VkBuffer map_indirect;
    VkBuffer count;
  } buffers;

  // RAND
  bci.size  = rs_mr.keyvals_size;
  bci.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT |  //
              VK_BUFFER_USAGE_TRANSFER_SRC_BIT;     // SRC for copying to keyval buffers

  vk(CreateBuffer(device, &bci, NULL, &buffers.rand));

  // EVEN/ODD
  bci.size  = rs_mr.keyvals_size;
  bci.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT |  //
              VK_BUFFER_USAGE_TRANSFER_SRC_BIT |    // SRC for debug copying back to host
              VK_BUFFER_USAGE_TRANSFER_DST_BIT |    // DST for initializing with rand
              VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;

  vk(CreateBuffer(device, &bci, NULL, &buffers.keyvals_even));
  vk(CreateBuffer(device, &bci, NULL, &buffers.keyvals_odd));

  // MAP KEYVALS
  bci.size  = rs_mr.keyvals_size;
  bci.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT |  //
              VK_BUFFER_USAGE_TRANSFER_DST_BIT;     // DST for debug copying back to host

  vk(CreateBuffer(device, &bci, NULL, &buffers.map_keyvals));

  // INTERNAL
  bci.size  = rs_mr.internal_size;
  bci.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT |  //
              VK_BUFFER_USAGE_TRANSFER_SRC_BIT |    // SRC for debug copying back to host
              VK_BUFFER_USAGE_TRANSFER_DST_BIT |    // DST for vkCmdFillBuffer()
              VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;

  vk(CreateBuffer(device, &bci, NULL, &buffers.internal));

  // MAP INTERNAL
  bci.size  = rs_mr.internal_size;
  bci.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT |  //
              VK_BUFFER_USAGE_TRANSFER_DST_BIT;     // DST for debug copying back to host

  vk(CreateBuffer(device, &bci, NULL, &buffers.map_internal));

  // INDIRECT
  bci.size  = rs_mr.indirect_size;
  bci.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT |   //
              VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT |  // Indirect buffer
              VK_BUFFER_USAGE_TRANSFER_SRC_BIT |     // SRC for debug copying back to host
              VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;

  vk(CreateBuffer(device, &bci, NULL, &buffers.indirect));

  // MAP INDIRECT
  bci.size  = rs_mr.indirect_size;
  bci.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT |  //
              VK_BUFFER_USAGE_TRANSFER_DST_BIT;     // DST for debug copying back to host

  vk(CreateBuffer(device, &bci, NULL, &buffers.map_indirect));

  // COUNT
  bci.size  = sizeof(uint32_t);
  bci.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT |  //
              VK_BUFFER_USAGE_TRANSFER_DST_BIT |    // DST for vkCmdUpdateBuffer()
              VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;

  vk(CreateBuffer(device, &bci, NULL, &buffers.count));

  //
  // create dbis
  //
  struct
  {
    VkDescriptorBufferInfo keyvals_even;
    VkDescriptorBufferInfo keyvals_odd;
    VkDescriptorBufferInfo internal;
    VkDescriptorBufferInfo indirect;
    VkDescriptorBufferInfo count;
  } dbis;

  dbis.keyvals_even = (VkDescriptorBufferInfo){

    .buffer = buffers.keyvals_even,
    .offset = 0,
    .range  = rs_mr.keyvals_size
  };

  dbis.keyvals_odd = (VkDescriptorBufferInfo){

    .buffer = buffers.keyvals_odd,
    .offset = 0,
    .range  = rs_mr.keyvals_size
  };

  dbis.internal = (VkDescriptorBufferInfo){

    .buffer = buffers.internal,
    .offset = 0,
    .range  = rs_mr.internal_size
  };

  dbis.indirect = (VkDescriptorBufferInfo){

    .buffer = buffers.indirect,
    .offset = 0,
    .range  = rs_mr.indirect_size
  };

  dbis.count = (VkDescriptorBufferInfo){

    .buffer = buffers.count,
    .offset = 0,
    .range  = sizeof(uint32_t)
  };

  //
  // get memory requirements for one of the buffers
  //
  struct
  {
    VkMemoryRequirements rand;
    VkMemoryRequirements keyvals_even;
    VkMemoryRequirements keyvals_odd;
    VkMemoryRequirements map_keyvals;
    VkMemoryRequirements internal;
    VkMemoryRequirements map_internal;
    VkMemoryRequirements indirect;
    VkMemoryRequirements map_indirect;
    VkMemoryRequirements count;
  } mrs;

  vkGetBufferMemoryRequirements(device, buffers.rand, &mrs.rand);
  vkGetBufferMemoryRequirements(device, buffers.keyvals_even, &mrs.keyvals_even);
  vkGetBufferMemoryRequirements(device, buffers.keyvals_odd, &mrs.keyvals_odd);
  vkGetBufferMemoryRequirements(device, buffers.map_keyvals, &mrs.map_keyvals);
  vkGetBufferMemoryRequirements(device, buffers.internal, &mrs.internal);
  vkGetBufferMemoryRequirements(device, buffers.map_internal, &mrs.map_internal);
  vkGetBufferMemoryRequirements(device, buffers.indirect, &mrs.indirect);
  vkGetBufferMemoryRequirements(device, buffers.map_indirect, &mrs.map_indirect);
  vkGetBufferMemoryRequirements(device, buffers.count, &mrs.count);

  //
  // indicate that we're going to get the buffer's address
  //
  VkMemoryAllocateFlagsInfo const afi_devaddr = {

    .sType      = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_FLAGS_INFO,
    .pNext      = NULL,
    .flags      = VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT_KHR,
    .deviceMask = 0
  };

  //
  // allocate memory for the buffers
  //
  // for simplicity, all buffers are the same size
  //
  VkMemoryAllocateInfo const mai_rand = {

    .sType           = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
    .pNext           = NULL,
    .allocationSize  = mrs.rand.size,
    .memoryTypeIndex = vk_find_mem_type_idx(&pdmp,
                                            mrs.rand.memoryTypeBits,
                                            (VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |  //
                                             VK_MEMORY_PROPERTY_HOST_COHERENT_BIT))
  };

  VkMemoryAllocateInfo const mai_keyvals_even = {

    .sType           = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
    .pNext           = &afi_devaddr,
    .allocationSize  = mrs.keyvals_even.size,
    .memoryTypeIndex = vk_find_mem_type_idx(&pdmp,  //
                                            mrs.keyvals_even.memoryTypeBits,
                                            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT)
  };

  VkMemoryAllocateInfo const mai_keyvals_odd = {

    .sType           = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
    .pNext           = &afi_devaddr,
    .allocationSize  = mrs.keyvals_odd.size,
    .memoryTypeIndex = vk_find_mem_type_idx(&pdmp,  //
                                            mrs.keyvals_odd.memoryTypeBits,
                                            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT)
  };

  VkMemoryAllocateInfo const mai_map_keyvals = {

    .sType           = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
    .pNext           = NULL,
    .allocationSize  = mrs.map_keyvals.size,
    .memoryTypeIndex = vk_find_mem_type_idx(&pdmp,
                                            mrs.map_keyvals.memoryTypeBits,
                                            (VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |  //
                                             VK_MEMORY_PROPERTY_HOST_COHERENT_BIT))
  };

  VkMemoryAllocateInfo const mai_internal = {

    .sType           = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
    .pNext           = &afi_devaddr,
    .allocationSize  = mrs.internal.size,
    .memoryTypeIndex = vk_find_mem_type_idx(&pdmp,  //
                                            mrs.internal.memoryTypeBits,
                                            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT)
  };

  VkMemoryAllocateInfo const mai_map_internal = {

    .sType           = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
    .pNext           = NULL,
    .allocationSize  = mrs.map_internal.size,
    .memoryTypeIndex = vk_find_mem_type_idx(&pdmp,
                                            mrs.map_internal.memoryTypeBits,
                                            (VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |  //
                                             VK_MEMORY_PROPERTY_HOST_COHERENT_BIT))
  };

  VkMemoryAllocateInfo const mai_indirect = {

    .sType           = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
    .pNext           = &afi_devaddr,
    .allocationSize  = mrs.indirect.size,
    .memoryTypeIndex = vk_find_mem_type_idx(&pdmp,  //
                                            mrs.indirect.memoryTypeBits,
                                            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT)
  };

  VkMemoryAllocateInfo const mai_map_indirect = {

    .sType           = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
    .pNext           = NULL,
    .allocationSize  = mrs.map_indirect.size,
    .memoryTypeIndex = vk_find_mem_type_idx(&pdmp,
                                            mrs.map_indirect.memoryTypeBits,
                                            (VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |  //
                                             VK_MEMORY_PROPERTY_HOST_COHERENT_BIT))
  };

  VkMemoryAllocateInfo const mai_count = {

    .sType           = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
    .pNext           = &afi_devaddr,
    .allocationSize  = mrs.count.size,
    .memoryTypeIndex = vk_find_mem_type_idx(&pdmp,  //
                                            mrs.count.memoryTypeBits,
                                            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT)
  };
  struct
  {
    VkDeviceMemory rand;
    VkDeviceMemory keyvals_even;
    VkDeviceMemory keyvals_odd;
    VkDeviceMemory map_keyvals;
    VkDeviceMemory internal;
    VkDeviceMemory map_internal;
    VkDeviceMemory indirect;
    VkDeviceMemory map_indirect;
    VkDeviceMemory count;
  } mems;

  vk(AllocateMemory(device, &mai_rand, NULL, &mems.rand));
  vk(AllocateMemory(device, &mai_keyvals_even, NULL, &mems.keyvals_even));
  vk(AllocateMemory(device, &mai_keyvals_odd, NULL, &mems.keyvals_odd));
  vk(AllocateMemory(device, &mai_map_keyvals, NULL, &mems.map_keyvals));
  vk(AllocateMemory(device, &mai_internal, NULL, &mems.internal));
  vk(AllocateMemory(device, &mai_map_internal, NULL, &mems.map_internal));
  vk(AllocateMemory(device, &mai_indirect, NULL, &mems.indirect));
  vk(AllocateMemory(device, &mai_map_indirect, NULL, &mems.map_indirect));
  vk(AllocateMemory(device, &mai_count, NULL, &mems.count));

  //
  // bind backing memory to the virtual allocations
  //
  vk(BindBufferMemory(device, buffers.rand, mems.rand, 0));
  vk(BindBufferMemory(device, buffers.keyvals_even, mems.keyvals_even, 0));
  vk(BindBufferMemory(device, buffers.keyvals_odd, mems.keyvals_odd, 0));
  vk(BindBufferMemory(device, buffers.map_keyvals, mems.map_keyvals, 0));
  vk(BindBufferMemory(device, buffers.internal, mems.internal, 0));
  vk(BindBufferMemory(device, buffers.map_internal, mems.map_internal, 0));
  vk(BindBufferMemory(device, buffers.indirect, mems.indirect, 0));
  vk(BindBufferMemory(device, buffers.map_indirect, mems.map_indirect, 0));
  vk(BindBufferMemory(device, buffers.count, mems.count, 0));

  //
  // create the rand and sort host buffers
  //
  void * rand_h = malloc(rs_mr.keyvals_size);
  void * sort_h = malloc(rs_mr.keyvals_size);

  rs_fill_rand(rand_h, count_hi, keyval_dwords);

  void * rand_map;

  vk(MapMemory(device, mems.rand, 0, VK_WHOLE_SIZE, 0, &rand_map));

  memcpy(rand_map, rand_h, rs_mr.keyvals_size);

  vkUnmapMemory(device, mems.rand);

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

  VkCommandBuffer cb, cb_rand_copy;

  vk(AllocateCommandBuffers(device, &cmd_buffer_info, &cb));
  vk(AllocateCommandBuffers(device, &cmd_buffer_info, &cb_rand_copy));

  //
  //
  //
  VkCommandBufferBeginInfo const cb_begin_info = {
    .sType            = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
    .pNext            = NULL,
    .flags            = 0,  // VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
    .pInheritanceInfo = NULL
  };

  struct VkSubmitInfo const submit_info_cb_rand_copy = {

    .sType                = VK_STRUCTURE_TYPE_SUBMIT_INFO,
    .pNext                = NULL,
    .waitSemaphoreCount   = 0,
    .pWaitSemaphores      = NULL,
    .pWaitDstStageMask    = NULL,
    .commandBufferCount   = 1,
    .pCommandBuffers      = &cb_rand_copy,
    .signalSemaphoreCount = 0,
    .pSignalSemaphores    = NULL
  };

  struct VkSubmitInfo const submit_info_cb = {

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
  // labels
  //
  fprintf(stdout,
          "Device, "
          "Driver, "
          "Dispatch, "
          "Keyval, "
          "Verified?, "
          "Count, "
          "CPU, "
          "Algo, "
          "Msecs, "
          "Mkeys/s, "
          "GPU, "
          "Trials, ");

  //
  // accumulate verifications
  //
  bool all_verified = true;

  //
  // Capture timestamps
  //
  struct radix_sort_vk_ext_timestamps ext_timestamps = {
    .ext             = NULL,
    .type            = RADIX_SORT_VK_EXT_TIMESTAMPS,
    .timestamp_count = QUERY_POOL_SIZE_MAX,
    .timestamps      = query_pool,
    .timestamps_set  = 0,
  };

  //
  // Benchmark a range
  //
  for (uint32_t count = count_lo; count <= count_hi; count += count_step)
    {
      //
      // Anything to do?
      //
      if (count <= 1)
        continue;

      //
      // Init elapsed time accumulators
      //
      uint64_t elapsed_min[QUERY_POOL_SIZE_MAX] = { [0 ... QUERY_POOL_SIZE_MAX - 1] = UINT64_MAX };
      uint64_t elapsed_max[QUERY_POOL_SIZE_MAX] = { 0 };
      uint64_t elapsed_sum[QUERY_POOL_SIZE_MAX] = { 0 };

      ext_timestamps.timestamps_set = 0;

      //////////////////////////////////////////////////////////////////
      //
      // COMMAND BUFFER RAND COPY
      //
      // Initialize buffer_even with random keys
      //
      vkBeginCommandBuffer(cb_rand_copy, &cb_begin_info);

      VkBufferCopy const copy_rand = {

        .srcOffset = 0,
        .dstOffset = 0,
        .size      = rs_mr.keyval_size * count
      };

      vkCmdCopyBuffer(cb_rand_copy, buffers.rand, buffers.keyvals_even, 1, &copy_rand);

      //
      // Initialize count
      //
      if (!is_direct)
        {
          vkCmdUpdateBuffer(cb_rand_copy, buffers.count, 0, sizeof(uint32_t), &count);
        }

      vk(EndCommandBuffer(cb_rand_copy));

      //////////////////////////////////////////////////////////////////
      //
      // COMMAND BUFFER SORT
      //
      // Define sort
      //
      vkBeginCommandBuffer(cb, &cb_begin_info);

      //
      // Reset the query pool
      //
      vkCmdResetQueryPool(cb, query_pool, 0, QUERY_POOL_SIZE_MAX);

      //
      // If timestamp splits aren't enabled then capture start.
      //
#ifndef RS_ENABLE_EXT_TIMESTAMPS
      vkCmdWriteTimestamp(cb,
                          VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                          ext_timestamps.timestamps,
                          ext_timestamps.timestamps_set++);
#endif

      //
      // Which dbi contains the sorted keyvals?
      //
      VkDescriptorBufferInfo const * dbi_keyvals_sorted;

      //
      // Direct or indirect radix sort?
      //
      if (is_direct)
        {
          //
          // DIRECT
          //
          struct radix_sort_vk_sort_info const info = {
#ifdef RS_ENABLE_EXT_TIMESTAMPS
            .ext = &ext_timestamps,
#else
            .ext = NULL,
#endif
            .key_bits     = keyval_bytes * 8,
            .count        = count,
            .keyvals_even = &dbis.keyvals_even,
            .keyvals_odd  = &dbis.keyvals_odd,
            .internal     = &dbis.internal
          };

          radix_sort_vk_sort(rs, &info, device, cb, &dbi_keyvals_sorted);
        }
      else
        {
          //
          // INDIRECT
          //
          struct radix_sort_vk_sort_indirect_info const info = {
#ifdef RS_ENABLE_EXT_TIMESTAMPS
            .ext = &ext_timestamps,
#else
            .ext = NULL,
#endif
            .key_bits     = keyval_bytes * 8,
            .count        = &dbis.count,
            .keyvals_even = &dbis.keyvals_even,
            .keyvals_odd  = &dbis.keyvals_odd,
            .internal     = &dbis.internal,
            .indirect     = &dbis.indirect
          };

          radix_sort_vk_sort_indirect(rs, &info, device, cb, &dbi_keyvals_sorted);
        }

        //
        // If timestamp splits aren't enabled then capture end.
        //
#ifndef RS_ENABLE_EXT_TIMESTAMPS
      vkCmdWriteTimestamp(cb,
                          VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                          ext_timestamps.timestamps,
                          ext_timestamps.timestamps_set++);
#endif

      vk(EndCommandBuffer(cb));

      //////////////////////////////////////////////////////////////////
      //
      // repeatedly submit in a tight loop
      //
      uint32_t const total_loops = warmup + loops;

      for (uint32_t loop_idx = 0; loop_idx < total_loops; loop_idx++)
        {
          // submit rand copy and wait
          vk(QueueSubmit(queue, 1, &submit_info_cb_rand_copy, VK_NULL_HANDLE));

          // wait for queue to drain
          vk(QueueWaitIdle(queue));

          // submit radix sort and wait
          vk(QueueSubmit(queue, 1, &submit_info_cb, VK_NULL_HANDLE));

          // wait for queue to drain
          vk(QueueWaitIdle(queue));

          //
          // read timestamps
          //
          if (loop_idx >= warmup)
            {
              uint64_t timestamps[QUERY_POOL_SIZE_MAX];

              vk(GetQueryPoolResults(device,
                                     query_pool,
                                     0,
                                     ext_timestamps.timestamps_set,
                                     sizeof(timestamps),
                                     timestamps,
                                     sizeof(timestamps[0]),
                                     VK_QUERY_RESULT_64_BIT | VK_QUERY_RESULT_WAIT_BIT));

              // split times
              for (uint32_t jj = 0; jj < ext_timestamps.timestamps_set - 1; jj++)
                {
                  uint64_t const t = timestamps[jj + 1] - timestamps[jj];

                  elapsed_min[jj] = MIN_MACRO(uint64_t, elapsed_min[jj], t);
                  elapsed_max[jj] = MAX_MACRO(uint64_t, elapsed_max[jj], t);
                  elapsed_sum[jj] += t;
                }

              // total time
              {
                uint32_t const last = ext_timestamps.timestamps_set - 1;
                uint64_t const t    = timestamps[last] - timestamps[0];

                elapsed_min[last] = MIN_MACRO(uint64_t, elapsed_min[last], t);
                elapsed_max[last] = MAX_MACRO(uint64_t, elapsed_max[last], t);
                elapsed_sum[last] += t;
              }
            }
        }

      vk(ResetCommandBuffer(cb_rand_copy, 0));
      vk(ResetCommandBuffer(cb, 0));

      //
      // timestamp labels
      //
      if (count == count_lo)
        {
          for (uint32_t ii = 0; ii < ext_timestamps.timestamps_set; ii++)
            {
              fprintf(stdout, "Min Msecs, ");
            }

          fprintf(stdout, "Max Mkeys/s\n");
        }

      //
      // copy the results back and, optionally, verify them
      //
      char const * cpu_algo = NULL;
      double       cpu_ns   = 0.0;
      bool         verified = true;

      if (verify)
        {
          //
          // get the internals buffer from the device
          //
          vkBeginCommandBuffer(cb, &cb_begin_info);

          //
          // get the sorted keys buffer from the device
          //
          VkBufferCopy const buffer_sorted_copy = {

            .srcOffset = dbi_keyvals_sorted->offset,
            .dstOffset = 0,
            .size      = dbi_keyvals_sorted->range
          };

          vkCmdCopyBuffer(cb,
                          dbi_keyvals_sorted->buffer,
                          buffers.map_keyvals,
                          1,
                          &buffer_sorted_copy);

          VkBufferCopy const buffer_internal_copy = {

            .srcOffset = dbis.internal.offset,
            .dstOffset = 0,
            .size      = dbis.internal.range
          };

          vkCmdCopyBuffer(cb, buffers.internal, buffers.map_internal, 1, &buffer_internal_copy);

          //
          // get the indirect buffer from the device
          //
          VkBufferCopy const buffer_indirect_copy = {

            .srcOffset = dbis.indirect.offset,
            .dstOffset = 0,
            .size      = dbis.indirect.range
          };

          vkCmdCopyBuffer(cb, buffers.indirect, buffers.map_indirect, 1, &buffer_indirect_copy);

          vk(EndCommandBuffer(cb));

          //
          // submit and wait until complete
          //
          vk(QueueSubmit(queue, 1, &submit_info_cb, VK_NULL_HANDLE));

          vk(QueueWaitIdle(queue));

          //
          // reuse the cb
          //
          vk(ResetCommandBuffer(cb, 0));

          // map sorted
          void * mem_map_keyvals_d;
          void * mem_map_internal_d;
          void * mem_map_indirect_d;

          vk(MapMemory(device, mems.map_keyvals, 0, VK_WHOLE_SIZE, 0, &mem_map_keyvals_d));
          vk(MapMemory(device, mems.map_internal, 0, VK_WHOLE_SIZE, 0, &mem_map_internal_d));
          vk(MapMemory(device, mems.map_indirect, 0, VK_WHOLE_SIZE, 0, &mem_map_indirect_d));

          //
          // sort on host
          //
          uint32_t     cpu_loops  = RS_BENCH_LOOPS_CPU;
          size_t const count_size = rs_mr.keyval_size * count;

          while (cpu_loops-- > 0)  // run the cpu_algo N times and measure last
            {
              // copy random data
              memcpy(sort_h, rand_h, count_size);

              // sort on the cpu
              cpu_algo = cpu_sort(sort_h,  //
                                  keyval_dwords,
                                  count,
                                  &cpu_ns);
            }

          //
          // verify
          //
          verified = memcmp(sort_h, mem_map_keyvals_d, count_size) == 0;

          //
          // DEBUG
          //
#ifdef RS_DEBUG_DUMP
          if (!is_direct)
            {
              rs_debug_dump_indirect(mem_map_indirect_d);
            }

          if (keyval_bytes == 4)
            {
              rs_debug_dump_histograms(mem_map_internal_d, keyval_bytes, keyval_bytes, 32);

              // TODO(allanmac): dump partitions

              fprintf(stdout, "KEYVALS (GOLD) %8u -------------\n", count);
              rs_debug_dump_keyvals_u32(sort_h, sort_h, 32, count);

              fprintf(stdout, "KEYVALS (SORT) %8u -------------\n", count);
              rs_debug_dump_keyvals_u32(mem_map_keyvals_d, sort_h, 32, count);
            }
          else if (keyval_bytes == 8)
            {
              rs_debug_dump_histograms(mem_map_internal_d, keyval_bytes, keyval_bytes, 16);
              // TODO(allanmac): dump partitions

              fprintf(stdout, "KEYVALS (GOLD) %8u -------------\n", count);
              rs_debug_dump_keyvals_u64(sort_h, sort_h, 16, count);

              fprintf(stdout, "KEYVALS (SORT) %8u -------------\n", count);
              rs_debug_dump_keyvals_u64(mem_map_keyvals_d, sort_h, 16, count);
            }
#endif

          //
          // done with buffer_sortedmap
          //
          vkUnmapMemory(device, mems.map_keyvals);
          vkUnmapMemory(device, mems.map_internal);
          vkUnmapMemory(device, mems.map_indirect);
        }

      //
      // any verification failures?
      //
      all_verified = all_verified && verified;

      //
      // timestamps are in nanoseconds
      //
      fprintf(stdout,
              "%s, %u.%u.%u, %s, %s, %s, %10u, CPU, %s, %9.3f, %6.2f, GPU, %9u, ",
              pdp.deviceName,
              // TODO(allanmac): Use undeprecated VK_API_VERSION_* once headers are updated
              VK_VERSION_MAJOR(pdp.driverVersion),
              VK_VERSION_MINOR(pdp.driverVersion),
              VK_VERSION_PATCH(pdp.driverVersion),
              is_direct ? "direct" : "indirect",
              (rs_mr.keyval_size == sizeof(uint32_t)) ? "uint" : "ulong",
              verify ? (verified ? "  OK  " : "*FAIL*") : "UNVERIFIED",
              count,
              // CPU
              verify ? cpu_algo : "UNVERIFIED",
              verify ? (cpu_ns / 1000000.0) : 0.0,       // milliseconds
              verify ? (1000.0 * count / cpu_ns) : 0.0,  // mkeys / sec
              loops);

      {
        double elapsed_ns_min_f64;

        for (uint32_t ii = 0; ii < ext_timestamps.timestamps_set; ii++)
          {
            elapsed_ns_min_f64 = (double)elapsed_min[ii] * vk_timestamp_period;

            fprintf(stdout, "%8.3f, ", elapsed_ns_min_f64 / 1e6);
          }

        fprintf(stdout, "%7.2f\n", 1000.0 * count / elapsed_ns_min_f64);
      }

      //
      // Make each trial visible ASAP...
      //
      fflush(stdout);
    }
  //
  // cleanup
  //

  // destroy radix sort instance
  radix_sort_vk_destroy(rs, device, NULL);

  // destroy buffers
  vkDestroyBuffer(device, buffers.rand, NULL);
  vkDestroyBuffer(device, buffers.keyvals_even, NULL);
  vkDestroyBuffer(device, buffers.keyvals_odd, NULL);
  vkDestroyBuffer(device, buffers.map_keyvals, NULL);
  vkDestroyBuffer(device, buffers.internal, NULL);
  vkDestroyBuffer(device, buffers.map_internal, NULL);
  vkDestroyBuffer(device, buffers.indirect, NULL);
  vkDestroyBuffer(device, buffers.map_indirect, NULL);
  vkDestroyBuffer(device, buffers.count, NULL);

  // free device memory
  vkFreeMemory(device, mems.rand, NULL);
  vkFreeMemory(device, mems.keyvals_even, NULL);
  vkFreeMemory(device, mems.keyvals_odd, NULL);
  vkFreeMemory(device, mems.map_keyvals, NULL);
  vkFreeMemory(device, mems.internal, NULL);
  vkFreeMemory(device, mems.map_internal, NULL);
  vkFreeMemory(device, mems.indirect, NULL);
  vkFreeMemory(device, mems.map_indirect, NULL);
  vkFreeMemory(device, mems.count, NULL);

  // free host memory
  free(rand_h);
  free(sort_h);

  // destroy query pool
  vkDestroyQueryPool(device, query_pool, NULL);

  // destroy command buffer and pool
  vkFreeCommandBuffers(device, cmd_pool, 1, &cb_rand_copy);
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
