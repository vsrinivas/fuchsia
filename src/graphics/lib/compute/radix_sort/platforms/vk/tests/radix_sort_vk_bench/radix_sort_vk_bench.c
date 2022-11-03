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
#include "common/vk/debug_utils.h"
#include "common/vk/find_mem_type_idx.h"
#include "common/vk/pipeline_cache.h"

//
//
//
#include "radix_sort/platforms/vk/radix_sort_vk.h"  // public

//
// This test is a friend of the radix sort library and uses private includes.
//
#include "find_target_name.h"
#include "radix_sort/platforms/vk/radix_sort_vk_ext.h"  // private
#include "radix_sort/platforms/vk/shaders/push.h"       // private
#include "radix_sort_vk_bench.h"

//
// Define a platform-specific prefix
//
#ifdef __Fuchsia__
#define VK_PIPELINE_CACHE_NAME "/cache/.radix_sort_vk_bench_cache"
#else
#define VK_PIPELINE_CACHE_NAME ".radix_sort_vk_bench_cache"
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
rs_rand_u32(uint32_t index)
{
#if 1  // (keyval_dwords * 32) bits
  static uint32_t seed = 0xDEADBEEF;

  // Numerical Recipes
  seed = seed * 1664525 + 1013904223;

  return seed;
#elif 0  // 18 bits
  static uint32_t seed = 0xDEADBEEF;

  // Numerical Recipes
  seed = seed * 1664525 + 1013904223;

  return seed & (((index & 1) == 0) ? 0 : 0xFFFFC000);
#else    // uniform
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
      vin_h[ii] = rs_rand_u32(ii);
    }
}

//
// Loadable vs. linkable target archives
//
//   - Loadable expects a path to a target archive ".ar" file
//   - Linkable expects the target name of the archive
//
#ifndef RS_VK_TARGET_ARCHIVE_LINKABLE

//
// Load the target binary.
//
// Must be freed by caller.
//
static struct radix_sort_vk_target const *
rs_load_target(char const * filename)
{
  FILE * file = fopen(filename, "rb");

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

#ifndef NDEBUG
  fprintf(stderr, "radix_sort_vk_target::file_size = %ld\n", file_size);
#endif

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

  struct radix_sort_vk_target * target = MALLOC_MACRO(file_size);

  if ((target != NULL) && (fread(target, 1, file_size, file) != (size_t)file_size))
    {
      fprintf(stderr, "Error: Can't read target filename \"%s\"\n", filename);
      fclose(file);
      free(target);
      return NULL;
    }

  fclose(file);

  return target;
}

#else  // RS_VK_TARGET_ARCHIVE_LINKABLE is defined

//
// Define a lookup table to map linked target symbols to names.
//
// clang-format off
struct rs_name_target
{
  char const * name;
  union
  {
    struct target_archive_header const * header;
    struct radix_sort_vk_target const *  target;
  };
};

#include "radix_sort_vk_amd_gcn3_u32_linkable.h"
#include "radix_sort_vk_amd_gcn3_u64_linkable.h"
#include "radix_sort_vk_arm_bifrost4_u32_linkable.h"
#include "radix_sort_vk_arm_bifrost4_u64_linkable.h"
#include "radix_sort_vk_arm_bifrost8_u32_linkable.h"
#include "radix_sort_vk_arm_bifrost8_u64_linkable.h"
#include "radix_sort_vk_google_swiftshader_u32_linkable.h"
#include "radix_sort_vk_google_swiftshader_u64_linkable.h"
#include "radix_sort_vk_intel_gen8_u32_linkable.h"
#include "radix_sort_vk_intel_gen8_u64_linkable.h"
#include "radix_sort_vk_nvidia_sm35_u32_linkable.h"
#include "radix_sort_vk_nvidia_sm35_u64_linkable.h"

static struct rs_name_target const rs_named_targets[] = {
  { .name = "amd_gcn3_u32",           .header = radix_sort_vk_amd_gcn3_u32_linkable           },
  { .name = "amd_gcn3_u64",           .header = radix_sort_vk_amd_gcn3_u64_linkable           },
  { .name = "arm_bifrost4_u32",       .header = radix_sort_vk_arm_bifrost4_u32_linkable       },
  { .name = "arm_bifrost4_u64",       .header = radix_sort_vk_arm_bifrost4_u64_linkable       },
  { .name = "arm_bifrost8_u32",       .header = radix_sort_vk_arm_bifrost8_u32_linkable       },
  { .name = "arm_bifrost8_u64",       .header = radix_sort_vk_arm_bifrost8_u64_linkable       },
  { .name = "google_swiftshader_u32", .header = radix_sort_vk_google_swiftshader_u32_linkable },
  { .name = "google_swiftshader_u64", .header = radix_sort_vk_google_swiftshader_u64_linkable },
  { .name = "intel_gen8_u32",         .header = radix_sort_vk_intel_gen8_u32_linkable         },
  { .name = "intel_gen8_u64",         .header = radix_sort_vk_intel_gen8_u64_linkable         },
  { .name = "nvidia_sm35_u32",        .header = radix_sort_vk_nvidia_sm35_u32_linkable        },
  { .name = "nvidia_sm35_u64",        .header = radix_sort_vk_nvidia_sm35_u64_linkable        },
};
// clang-format on

//
// No matching target name
//
static void
rs_list_targets(void)
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
rs_usage(char const * exec_name)
{
  fprintf(
    stderr,
    "\n"
    "Usage: %s "
    "<vendorID>:<deviceID> "
#if defined(__Fuchsia__) && !defined(RS_VK_TARGET_ARCHIVE_LINKABLE)
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
    "[validate? "
    "[validation?] ] ] ] ] ]\n\n"

    "  <vendorID>:<deviceID> : Execute on a specific Vulkan physical device.\n"
#if defined(__Fuchsia__) && !defined(RS_VK_TARGET_ARCHIVE_LINKABLE)
    "  <target file>         : Name of Radix Sort target file: \"pkg/data/targets/radix_sort_vk_<vendor name>_<arch name>_<u32 or u64>_resource.ar\".\n"
#else
    "  <target name>         : Name of Radix Sort target: <vendor name>_<arch name>_<u32 or u64>.\n"
#endif
    "  <\"direct\"|\"indirect\"> : Direct or indirect radix sort.\n"
    "  <count lo>            : Initial number of keyvals.\n"
    "  <count hi>            : Final number of keyvals.\n"
    "  <count step>          : Keyval step size.\n"
    "  <iterations>          : Number of times each step is executed in the benchmark.\n"
    "  <warmup>              : Number of times each step is executed before starting the benchmark.\n"
    "  <verify?>             : If 0 then skip verifying the device sorted results against a CPU sorting algorithm. Default 1.\n"
    "  <validate?>           : If 0 then skip loading the Vulkan Validation layers. Default 0.\n\n",
    exec_name);

#ifdef RS_VK_TARGET_ARCHIVE_LINKABLE
  rs_list_targets();
#endif
}

//
// Parse args as defined in `rs_usage()`
//
// Parsing always succeeds.
//
void
radix_sort_vk_bench_parse(int argc, char const * argv[], struct radix_sort_vk_bench_info * info)
{
  //
  // Zero the struct
  //
  memset(info, 0, sizeof(*info));

  //
  // Save the exec name
  //
  info->exec_name = argv[0];

  //
  // Select the target by vendor and device id.
  //
  if (argc > 1)
    {
      char * str_end;

      info->vendor_id = (uint32_t)strtoul(argv[1], &str_end, 16);  // returns 0 on error

      if (str_end != argv[1])
        {
          if (*str_end == ':')
            {
              info->device_id = (uint32_t)strtoul(str_end + 1, NULL, 16);  // returns 0 on error
            }
        }
    }

  //
  // Make sure there is a target filename
  //
  if (argc >= 3)
    {
      info->target_name = argv[2];
    }

  //
  // These boolean flags are kept separate because either a "direct" or
  // "indirect" keyword must be found when parsing arguments.
  //
  if (argc >= 4)
    {
      if (strcmp(argv[3], "direct") == 0)
        {
          info->is_direct = true;
        }
      else if (strcmp(argv[3], "indirect") == 0)
        {
          info->is_indirect = true;
        }
    }

  //
  // Parse the remaining command line args.
  //
  info->count_lo       = (argc <= 4) ? 1024 : (uint32_t)strtoul(argv[4], NULL, 0);
  info->count_hi       = (argc <= 5) ? info->count_lo : (uint32_t)strtoul(argv[5], NULL, 0);
  info->count_step     = (argc <= 6) ? info->count_lo : (uint32_t)strtoul(argv[6], NULL, 0);
  info->loops          = (argc <= 7) ? RS_BENCH_LOOPS : (uint32_t)strtoul(argv[7], NULL, 0);
  info->warmup         = (argc <= 8) ? RS_BENCH_WARMUP : (uint32_t)strtoul(argv[8], NULL, 0);
  info->is_verify      = (argc <= 9) ? true : strtoul(argv[9], NULL, 0) != 0;
  info->is_validation  = (argc <= 10) ? false : strtoul(argv[10], NULL, 0) != 0;
  info->is_debug_utils = true;
  info->is_verbose     = true;
}

//
// Benchmark
//
int
radix_sort_vk_bench(struct radix_sort_vk_bench_info const * info)
{
  //
  // Create a Vulkan 1.2 instance
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

  char const * const instance_layers[]     = { "VK_LAYER_KHRONOS_validation" };
  char const * const instance_extensions[] = { VK_EXT_DEBUG_UTILS_EXTENSION_NAME };

  uint32_t const instance_layer_count     = ARRAY_LENGTH_MACRO(instance_layers);
  uint32_t const instance_extension_count = ARRAY_LENGTH_MACRO(instance_extensions);

  assert(instance_layer_count >= 1);
  assert(instance_extension_count >= 1);

  VkInstanceCreateInfo const instance_info = {

    .sType                   = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
    .pNext                   = NULL,
    .flags                   = 0,
    .pApplicationInfo        = &app_info,
    .enabledLayerCount       = info->is_validation  //
                                 ? instance_layer_count
                                 : instance_layer_count - 1,
    .ppEnabledLayerNames     = instance_layers,
    .enabledExtensionCount   = info->is_debug_utils  //
                                 ? instance_extension_count
                                 : instance_extension_count - 1,
    .ppEnabledExtensionNames = instance_extensions
  };

  VkInstance instance;

  VkResult const result = vkCreateInstance(&instance_info, NULL, &instance);

  if (result != VK_SUCCESS)
    {
      fprintf(stderr, "Couldn't create VkInstance.\n");

      return EXIT_FAILURE;
    }

  //
  // Init debug utils?
  //
  if (info->is_debug_utils)
    {
      vk_debug_utils_init(instance);
    }

  //
  // Acquire all physical devices and select a match.
  //
  uint32_t pd_count;

  vk(EnumeratePhysicalDevices(instance, &pd_count, NULL));

  if (pd_count == 0)
    {
      fprintf(stderr, "VkPhysicalDevice count is zero.\n");

      vkDestroyInstance(instance, NULL);

      return EXIT_FAILURE;
    }

  VkPhysicalDevice * pds = MALLOC_MACRO(pd_count * sizeof(*pds));

  vk(EnumeratePhysicalDevices(instance, &pd_count, pds));

  //
  // Use first physical device if 0:0.
  //
  uint32_t vendor_id, device_id;

  if ((info->vendor_id == 0) && (info->device_id == 0))
    {
      VkPhysicalDeviceProperties tmp;

      vkGetPhysicalDeviceProperties(pds[0], &tmp);

      vendor_id = tmp.vendorID;
      device_id = tmp.deviceID;
    }
  else
    {
      vendor_id = info->vendor_id;
      device_id = info->device_id;
    }

  //
  // Scan physical devices for a match.
  //
  VkPhysicalDevice           pd = VK_NULL_HANDLE;
  VkPhysicalDeviceProperties pdp;

  for (uint32_t ii = 0; ii < pd_count; ii++)
    {
      VkPhysicalDeviceProperties tmp;

      vkGetPhysicalDeviceProperties(pds[ii], &tmp);

      bool const is_match = (tmp.vendorID == vendor_id) && (tmp.deviceID == device_id);

      if (info->is_verbose)
        {
          fprintf(stdout,
                  "%c %c %8X : %-8X : %s\n",
                  is_match ? '*' : ' ',
                  is_match && info->is_validation ? 'V' : ' ',
                  tmp.vendorID,
                  tmp.deviceID,
                  tmp.deviceName);
        }

      if (is_match)
        {
          pd = pds[ii];
          memcpy(&pdp, &tmp, sizeof(tmp));
        }
    }

  if (info->is_verbose)
    {
      fprintf(stdout, "\n");
    }

  free(pds);

  if (pd == VK_NULL_HANDLE)
    {
      fprintf(stderr, "Error: Device %4X:%X not found\n", vendor_id, device_id);

      rs_usage(info->exec_name);

      vkDestroyInstance(instance, NULL);

      return EXIT_FAILURE;
    }

  //
  // Timestamp support is required.
  //
  if (pdp.limits.timestampComputeAndGraphics != VK_TRUE)
    {
      fprintf(stderr, "Error: Vulkan `pdp.limits.timestampComputeAndGraphics != VK_TRUE`\n");

      rs_usage(info->exec_name);

      vkDestroyInstance(instance, NULL);

      return EXIT_FAILURE;
    }

  //
  // Is there a target name?
  //
  char const * target_name;

  if (info->target_name == NULL)
    {
      target_name = radix_sort_vk_find_target_name(vendor_id, device_id, info->keyval_dwords);
    }
  else
    {
      target_name = info->target_name;
    }

  if (target_name == NULL)
    {
#if defined(__Fuchsia__) && !defined(RS_VK_TARGET_ARCHIVE_LINKABLE)
      fprintf(stderr, "Error: Missing target filename\n");
#else
      fprintf(stderr, "Error: Missing target name\n");
#endif

      rs_usage(info->exec_name);

      vkDestroyInstance(instance, NULL);

      return EXIT_FAILURE;
    }

  //
  // There must be either a direct or an indirect flag but not both.
  //
  if ((info->is_direct != info->is_indirect) == false)
    {
      fprintf(stderr, "Error: Specify either \"direct\" or \"indirect\"\n");

      rs_usage(info->exec_name);

      vkDestroyInstance(instance, NULL);

      return EXIT_FAILURE;
    }

  //
  // Loop validation
  //
  if (info->count_hi == 0)
    {
      if (info->is_verbose)
        {
          fprintf(stderr, "Error: count_hi must be >= 1\n");

          rs_usage(info->exec_name);
        }

      vkDestroyInstance(instance, NULL);

      return EXIT_FAILURE;
    }

  if (info->count_lo > info->count_hi)
    {
      fprintf(stderr, "Error: count_lo > count_hi\n");

      rs_usage(info->exec_name);

      vkDestroyInstance(instance, NULL);

      return EXIT_FAILURE;
    }

  if (info->loops == 0)
    {
      fprintf(stderr, "Error: Loops must be non-zero\n");

      rs_usage(info->exec_name);

      vkDestroyInstance(instance, NULL);

      return EXIT_FAILURE;
    }

  //
  // Load the target
  //
  struct radix_sort_vk_target const * rs_target = rs_load_target(target_name);

  if (rs_target == NULL)
    {
      fprintf(stderr, "Error: Target \"%s\" not found\n", target_name);

      rs_usage(info->exec_name);

      vkDestroyInstance(instance, NULL);

      return EXIT_FAILURE;
    }

  //
  // Get the physical device's memory props
  //
  VkPhysicalDeviceMemoryProperties pdmp;

  vkGetPhysicalDeviceMemoryProperties(pd, &pdmp);

  //
  // Get queue properties
  //
  uint32_t qfp_count;

  vkGetPhysicalDeviceQueueFamilyProperties(pd, &qfp_count, NULL);

  VkQueueFamilyProperties qfp[qfp_count];

  vkGetPhysicalDeviceQueueFamilyProperties(pd, &qfp_count, qfp);

  //
  // One compute queue
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
  // Probe Radix Sort device extension requirements for this target
  //
  struct radix_sort_vk_target_requirements rs_tr = { 0 };

  radix_sort_vk_target_get_requirements(rs_target, &rs_tr);  // returns false

  //
  // Feature structures chain
  //
  VkPhysicalDeviceVulkan12Features pdf12 = {
    .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES,
  };

  VkPhysicalDeviceVulkan11Features pdf11 = {
    .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_FEATURES,
    .pNext = &pdf12,
  };

  VkPhysicalDeviceFeatures2 pdf2 = {
    .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2,
    .pNext = &pdf11,
  };

  //
  // Populate Radix Sort device requirements
  //
  char const * ext_names[rs_tr.ext_name_count];

  rs_tr.ext_names = ext_names;
  rs_tr.pdf       = &pdf2.features;
  rs_tr.pdf11     = &pdf11;
  rs_tr.pdf12     = &pdf12;

  if (radix_sort_vk_target_get_requirements(rs_target, &rs_tr) != true)
    {
      fprintf(stderr, "Error: radix_sort_vk_target_get_requirements(...) != true\n");

      exit(EXIT_FAILURE);  // No easy recovery after this point
    }

  //
  // Create VkDevice
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
    .pEnabledFeatures        = NULL,
  };

  VkDevice device;

  vk(CreateDevice(pd, &device_info, NULL, &device));

  //
  // Get a queue
  //
  VkQueue queue;

  vkGetDeviceQueue(device, 0, 0, &queue);

  //
  // Get the pipeline cache
  //
  VkPipelineCache pc;

  vk_pipeline_cache_create(device, NULL, VK_PIPELINE_CACHE_NAME, &pc);

  //
  // Create Radix Sort instance
  //
  struct radix_sort_vk * const rs = radix_sort_vk_create(device, NULL, pc, rs_target);

  //
  // Destroy the pipeline cache
  //
  vk_pipeline_cache_destroy(device, NULL, VK_PIPELINE_CACHE_NAME, pc);

  //
  // Free the target archive if it was loaded
  //
#ifndef RS_VK_TARGET_ARCHIVE_LINKABLE
  free((void *)rs_target);
#endif

  //
  // Was the radix sort instance successfully created?
  //
  if (rs == NULL)
    {
      fprintf(stderr, "Failed to create Radix Sort target!\n");

      exit(EXIT_FAILURE);  // No easy recovery after this point
    }

  //
  // Create command pool
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
  // Create a query pool for benchmarking
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
    .pipelineStatistics = 0,
  };

  VkQueryPool query_pool;

  vk(CreateQueryPool(device, &query_pool_info, NULL, &query_pool));

  //
  // Get target's memory requirements
  //
  struct radix_sort_vk_memory_requirements rs_mr;

  radix_sort_vk_get_memory_requirements(rs, info->count_hi, &rs_mr);

  uint32_t const keyval_bytes  = (uint32_t)rs_mr.keyval_size;
  uint32_t const keyval_dwords = keyval_bytes / 4;

  //
  // Create buffers:
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
    .pQueueFamilyIndices   = NULL,
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
  // Create dbis
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
  // Get memory requirements for one of the buffers
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
  // Indicate that we're going to get the buffer's address
  //
  VkMemoryAllocateFlagsInfo const afi_devaddr = {

    .sType      = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_FLAGS_INFO,
    .pNext      = NULL,
    .flags      = VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT_KHR,
    .deviceMask = 0
  };

  //
  // Allocate memory for the buffers
  //
  // For simplicity, all buffers are the same size
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

  //
  // Allocate device memory
  //
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
  // Create the rand and sort host buffers
  //
  void * rand_h = MALLOC_MACRO(rs_mr.keyvals_size);
  void * sort_h = MALLOC_MACRO(rs_mr.keyvals_size);

  rs_fill_rand(rand_h, info->count_hi, keyval_dwords);

  void * rand_map;

  vk(MapMemory(device, mems.rand, 0, VK_WHOLE_SIZE, 0, &rand_map));

  memcpy(rand_map, rand_h, rs_mr.keyvals_size);

  vkUnmapMemory(device, mems.rand);

  //
  // Create a single command buffer for this thread
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
  // Labels
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
          "Usecs, "
          "Mkeys/s, "
          "GPU, "
          "Warmup, "
          "Trials, ");

  //
  // Accumulate verifications
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
  for (uint32_t count = info->count_lo; count <= info->count_hi; count += info->count_step)
    {
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

      if (count > 0)
        {
          VkBufferCopy const copy_rand = {

            .srcOffset = 0,
            .dstOffset = 0,
            .size      = rs_mr.keyval_size * count
          };

          vkCmdCopyBuffer(cb_rand_copy, buffers.rand, buffers.keyvals_even, 1, &copy_rand);
        }

      //
      // Initialize count
      //
      if (info->is_indirect)
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
      VkDescriptorBufferInfo dbi_keyvals_sorted;

      //
      // Direct or indirect radix sort?
      //
      if (!info->is_indirect)
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
            .keyvals_even = dbis.keyvals_even,
            .keyvals_odd  = dbis.keyvals_odd,
            .internal     = dbis.internal
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
            .count        = dbis.count,
            .keyvals_even = dbis.keyvals_even,
            .keyvals_odd  = dbis.keyvals_odd,
            .internal     = dbis.internal,
            .indirect     = dbis.indirect
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
      uint32_t const total_loops = info->warmup + info->loops;

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
          if (loop_idx >= info->warmup)
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

              //
              // there are either 0 timestamps or at least 2 timestamps
              //
              if (ext_timestamps.timestamps_set > 0)
                {
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
        }

      vk(ResetCommandBuffer(cb_rand_copy, 0));
      vk(ResetCommandBuffer(cb, 0));

      //
      // timestamp labels
      //
      if (count == info->count_lo)
        {
          //
          // there are either 0 timestamps or at least 2 timestamps
          //
          if (ext_timestamps.timestamps_set > 0)
            {
              for (uint32_t ii = 0; ii < ext_timestamps.timestamps_set - 1; ii++)
                {
                  fprintf(stdout, "Min Split Usecs, ");
                }

              fprintf(stdout, "Min Elapsed Usecs, ");
            }

          fprintf(stdout, "Max Mkeys/s\n");
        }

      //
      // copy the results back and, optionally, verify them
      //
      char const * cpu_algo = NULL;
      double       cpu_ns   = 0.0;
      bool         verified = true;

      if (info->is_verify)
        {
          //
          // get the internals buffer from the device
          //
          vkBeginCommandBuffer(cb, &cb_begin_info);

          //
          // get the sorted keys buffer from the device
          //
          VkBufferCopy const buffer_sorted_copy = {

            .srcOffset = dbi_keyvals_sorted.offset,
            .dstOffset = 0,
            .size      = dbi_keyvals_sorted.range
          };

          vkCmdCopyBuffer(cb,
                          dbi_keyvals_sorted.buffer,
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
          if (!info->is_direct)
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
              "%s, %u.%u.%u.%u, %s, %s, %s, %10u, CPU, %s, %12.3f, %7.2f, GPU, %9u, %9u, ",
              pdp.deviceName,
              VK_API_VERSION_VARIANT(pdp.driverVersion),
              VK_API_VERSION_MAJOR(pdp.driverVersion),
              VK_API_VERSION_MINOR(pdp.driverVersion),
              VK_API_VERSION_PATCH(pdp.driverVersion),
              info->is_indirect ? "indirect" : "direct",
              (rs_mr.keyval_size == sizeof(uint32_t)) ? "uint" : "ulong",
              info->is_verify ? (verified ? "    OK" : "*FAIL*") : "UNVERIFIED",
              count,
              // CPU
              info->is_verify ? cpu_algo : "UNVERIFIED",
              info->is_verify ? (cpu_ns / 1e3) : 0.0,             // usecs
              info->is_verify ? (1000.0 * count / cpu_ns) : 0.0,  // mkeys / sec
              info->warmup,
              info->loops);

      {
        double elapsed_ns_min_f64;

        for (uint32_t ii = 0; ii < ext_timestamps.timestamps_set; ii++)
          {
            elapsed_ns_min_f64 = (double)elapsed_min[ii] * vk_timestamp_period;

            fprintf(stdout, "%12.3f, ", elapsed_ns_min_f64 / 1e3);
          }

        fprintf(stdout, "%7.2f\n", 1000.0 * count / elapsed_ns_min_f64);
      }

      //
      // make each trial visible ASAP...
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

  // destroy device
  vkDestroyDevice(device, NULL);

  // destroy instance
  vkDestroyInstance(instance, NULL);

  return all_verified ? EXIT_SUCCESS : EXIT_FAILURE;
}

//
//
//
