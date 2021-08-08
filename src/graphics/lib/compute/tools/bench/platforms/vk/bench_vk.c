// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//
//
//
#include "bench_vk.h"

#include <assert.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "common/macros.h"
#include "common/vk/assert.h"
#include "common/vk/debug_utils.h"
#include "common/vk/find_mem_type_idx.h"

//
// NOOP shader module
//
#include "spirv_modules_rodata.h"

//
// Windows support
//
#ifdef _WIN32
#include <windows.h>
#endif

//
//
//
struct bench_vk
{
  VkInstance                       i;
  VkPhysicalDevice                 pd;
  VkPhysicalDeviceProperties       pdp;
  VkPhysicalDeviceMemoryProperties pdmp;
  VkDevice                         d;
  VkQueue                          q;
  VkCommandPool                    cp;
  VkQueryPool                      qp;
};

//
// Will be NULL if VK_EXT_calibrated_timestamps isn't supported
//
PFN_vkGetCalibratedTimestampsEXT pfn_vkGetCalibratedTimestampsEXT;

//
//
//
struct bench_config
{
  struct bench_vk vk;
  uint32_t        vendor_id;
  uint32_t        device_id;
  bool            is_quiet;
  bool            is_validate;
  bool            is_summary;
  bool            is_calibrated;  // Is VK_EXT_calibrated_timestamps present?
};

//
//
//
typedef enum bench_unit
{
  BENCH_UNIT_BYTES,
  BENCH_UNIT_KBYTES,
  BENCH_UNIT_MBYTES,
  BENCH_UNIT_GBYTES

} bench_unit_e;

//
//
//
typedef enum bench_wait
{
  BENCH_WAIT_FENCE,
  BENCH_WAIT_QUEUE,
  BENCH_WAIT_TIMELINE,

} bench_wait_e;

//
//
//
// clang-format off
#define BENCH_CONFIG_DEFAULT_QUEUE_FAMILY_INDEX 0
#define BENCH_CONFIG_DEFAULT_COUNT              256
#define BENCH_CONFIG_DEFAULT_REPETITIONS        20
#define BENCH_CONFIG_DEFAULT_WARMUP             1000
// clang-format on

//
//
//
struct bench_config_iter
{
  uint32_t repetitions;
  uint32_t warmup;
};

//
//
//
struct bench_config_fill
{
  uint32_t                 value;
  uint32_t                 count;
  bench_unit_e             unit;
  struct bench_config_iter iter;
  bench_wait_e             wait;
};

//
//
//
struct bench_config_copy
{
  uint32_t                 count;
  bench_unit_e             unit;
  struct bench_config_iter iter;
  bench_wait_e             wait;
};

//
//
//
struct bench_config_noop
{
  struct bench_config_iter iter;
  bench_wait_e             wait;
};

//
//
//
struct bench_split
{
  struct
  {
    uint64_t start;
    uint64_t stop;
  } device;

  struct
  {
    uint64_t start;
    uint64_t stop;
  } host;
};

//
//
//
union bench_timestamp_calibration
{
  struct
  {
    uint64_t timestamps[2];
    uint64_t max_deviations[2];
  } array;

  struct
  {
    struct
    {
      uint64_t device;
      uint64_t host;
    } timestamps;

    struct
    {
      uint64_t device;
      uint64_t host;
    } max_deviations;
  } named;
};

//
//
//
char const *
bench_wait_to_string(bench_wait_e const wait)
{
  switch (wait)
    {
      case BENCH_WAIT_FENCE:
        return "FENCE";

      case BENCH_WAIT_QUEUE:
        return "QUEUE";

      case BENCH_WAIT_TIMELINE:
        return "TIMELINE";
    };
}

//
// Capture a host timestamp
//
static void
bench_timestamp(uint64_t * const timestamp)
{
#ifndef _WIN32
  //
  // POSIX
  //
  struct timespec ts;

  clock_gettime(CLOCK_MONOTONIC_RAW, &ts);  // ignore return

  *timestamp = 1000000000 * ts.tv_sec + ts.tv_nsec;
#else
  //
  // WIN32
  //
  LARGE_INTEGER counter;

  QueryPerformanceCounter(&counter);

  *timestamp = (uint64_t)counter.QuadPart;
#endif
}

//
// Capture calibrated timestamps (once)
//
static void
bench_calibration(struct bench_config const *         config,
                  union bench_timestamp_calibration * calibration)
{
  assert(config->is_calibrated);

  //
  // FIXME(allanmac): On Win32 use QueryPerformanceCounter() and
  // VK_TIME_DOMAIN_QUERY_PERFORMANCE_COUNTER_EXT
  //
  VkCalibratedTimestampInfoEXT const infos[] = {
    { .sType      = VK_STRUCTURE_TYPE_CALIBRATED_TIMESTAMP_INFO_EXT,
      .pNext      = NULL,
      .timeDomain = VK_TIME_DOMAIN_DEVICE_EXT },
#ifndef _WIN32
    //
    // POSIX clock_gettime(CLOCK_MONOTONIC_RAW)
    //
    { .sType      = VK_STRUCTURE_TYPE_CALIBRATED_TIMESTAMP_INFO_EXT,
      .pNext      = NULL,
      .timeDomain = VK_TIME_DOMAIN_CLOCK_MONOTONIC_RAW_EXT }
#else
    //
    // WIN32 QueryPerformanceCounter()
    //
    { .sType      = VK_STRUCTURE_TYPE_CALIBRATED_TIMESTAMP_INFO_EXT,
      .pNext      = NULL,
      .timeDomain = VK_TIME_DOMAIN_QUERY_PERFORMANCE_COUNTER_EXT }
#endif
  };

  vk_ok(pfn_vkGetCalibratedTimestampsEXT(config->vk.d,
                                         ARRAY_LENGTH_MACRO(infos),
                                         infos,
                                         calibration->array.timestamps,
                                         calibration->array.max_deviations));
}

//
//
//
#ifndef _WIN32
//
// POSIX
//
#define BENCH_HOST_NS(host_) ((double)(host_))
#else
//
// WIN32
//
#define BENCH_WIN32_QPC_FREQUENCY_INIT()                                                           \
  double qpc_frequency;                                                                            \
  {                                                                                                \
    LARGE_INTEGER freq;                                                                            \
    QueryPerformanceFrequency(&freq);                                                              \
    qpc_frequency = (double)freq.QuadPart;                                                         \
  }

#define BENCH_HOST_NS(host_) (((double)(host_)*1e9) / qpc_frequency)
#endif

//
//
//
#define BENCH_DEVICE_NS(device_) ((double)(device_)*config->vk.pdp.limits.timestampPeriod)

//
//
//
static void
bench_statistics(struct bench_config const * const      config,
                 char const * const                     name,
                 VkDeviceSize const                     bytes,
                 struct bench_config_iter const * const iter,
                 bench_wait_e const                     wait,
                 struct bench_split const * const       splits)

{
#ifdef WIN32
  //
  // WIN32
  //
  BENCH_WIN32_QPC_FREQUENCY_INIT();
#endif

  //
  // Emit CSV
  //
  if (!config->is_summary)
    {
      if (config->is_calibrated)
        {
          union bench_timestamp_calibration calibration;

          bench_calibration(config, &calibration);

          if (!config->is_quiet)
            {
              fprintf(stdout,
                      "Device, "
                      "Driver, "
                      "Benchmark, "
                      "Wait, "
                      "Calibrated?, "
                      "Repetitions, "
                      "Warmup, "
                      "Bytes, "
                      "GBytes/sec, "
                      "Host Start Msecs, "
                      "Device Start Msecs, "
                      "Device Stop Msecs, "
                      "Host Stop MSecs, "
                      "(Device Start - Host Start) MSecs, "
                      "(Device Stop - Device Start) MSecs, "
                      "(Host Stop - Device Stop) Msecs, "
                      "(Host - Device) Msecs\n");
            }

          double const h_ns_calib   = BENCH_HOST_NS(calibration.named.timestamps.host);
          double const d_ns_calib   = BENCH_DEVICE_NS(calibration.named.timestamps.device);
          double const d_ns_to_h_ns = h_ns_calib - d_ns_calib;

          for (uint32_t ii = 0; ii < iter->repetitions; ii++)
            {
              if (!config->is_quiet)
                {
                  fprintf(
                    stdout,
                    "%s, %u.%u.%u, %s, %s, %s, %u, %u, %zu, ",
                    config->vk.pdp.deviceName,
                    // TODO(allanmac): Use undeprecated VK_API_VERSION_* once headers are updated
                    VK_VERSION_MAJOR(config->vk.pdp.driverVersion),
                    VK_VERSION_MINOR(config->vk.pdp.driverVersion),
                    VK_VERSION_PATCH(config->vk.pdp.driverVersion),
                    name,
                    bench_wait_to_string(wait),
                    config->is_calibrated ? "TRUE " : "FALSE",
                    iter->repetitions,
                    iter->warmup,
                    bytes);
                }

              double const h_ns_start   = BENCH_HOST_NS(splits[ii].host.start);
              double const h_ns_stop    = BENCH_HOST_NS(splits[ii].host.stop);
              double const h_ns_elapsed = (h_ns_stop - h_ns_start);

              // clang-format off
              double const d_h_ns_start   = BENCH_DEVICE_NS(splits[ii].device.start) + d_ns_to_h_ns;
              double const d_h_ns_stop    = BENCH_DEVICE_NS(splits[ii].device.stop) + d_ns_to_h_ns;
              double const d_h_ns_elapsed = (d_h_ns_stop - d_h_ns_start);
              // clang-format on

              if (!config->is_quiet)
                {
                  fprintf(
                    stdout,
                    "%8.3f, %10.3f, %10.3f, %10.3f, %10.3f, %+10.3f, %+10.3f, %+10.3f, %+10.3f\n",
                    (double)bytes / (d_h_ns_elapsed),
                    0.0,
                    (d_h_ns_start - h_ns_start) / 1e6,
                    (d_h_ns_stop - h_ns_start) / 1e6,
                    (h_ns_stop - h_ns_start) / 1e6,
                    (d_h_ns_start - h_ns_start) / 1e6,
                    (d_h_ns_stop - d_h_ns_start) / 1e6,
                    (h_ns_stop - d_h_ns_stop) / 1e6,
                    (h_ns_elapsed - d_h_ns_elapsed) / 1e6);
                }
            }
        }
      else
        {
          if (!config->is_quiet)
            {
              fprintf(stdout,
                      "Device, "
                      "Driver, "
                      "Benchmark, "
                      "Wait, "
                      "Calibrated?, "
                      "Repetitions, "
                      "Warmup, "
                      "Bytes, "
                      "GBytes/sec, "
                      "(Host Stop - Host Start) MSecs, "
                      "(Device Stop - Device Start) MSecs, "
                      "(Host - Device) Msecs\n");
            }

          for (uint32_t ii = 0; ii < iter->repetitions; ii++)
            {
              if (!config->is_quiet)
                {
                  fprintf(
                    stdout,
                    "%s, %u.%u.%u, %s, %s, %s, %u, %u, %zu, ",
                    config->vk.pdp.deviceName,
                    // TODO(allanmac): Use undeprecated VK_API_VERSION_* once headers are updated
                    VK_VERSION_MAJOR(config->vk.pdp.driverVersion),
                    VK_VERSION_MINOR(config->vk.pdp.driverVersion),
                    VK_VERSION_PATCH(config->vk.pdp.driverVersion),
                    name,
                    bench_wait_to_string(wait),
                    config->is_calibrated ? "TRUE " : "FALSE",
                    iter->repetitions,
                    iter->warmup,
                    bytes);
                }

              // clang-format off
              double const h_ns_elapsed = BENCH_HOST_NS(splits[ii].host.stop   - splits[ii].host.start);
              double const d_ns_elapsed = BENCH_HOST_NS(splits[ii].device.stop - splits[ii].device.start);
              // clang-format on

              if (!config->is_quiet)
                {
                  fprintf(stdout,
                          "%8.3f, %+10.3f, %+10.3f, %+10.3f\n",
                          (double)bytes / d_ns_elapsed,
                          h_ns_elapsed / 1e6,
                          d_ns_elapsed / 1e6,
                          (h_ns_elapsed - d_ns_elapsed) / 1e6);
                }
            }
        }
    }
  else
    {
      fprintf(stderr, "SUMMARY STATISTICS ARE UNIMPLEMENTED\n");
    }
}

//
// Usage
//
void
bench_vk_usage(char const * argv[])
{
  //
  // Arguments are greedily parsed.
  //
  // Usage: bench
  //        ["quiet"]                                    - Only print errors
  //        ["validate"]                                 - Enable Vulkan Validation Layers
  //        ["summary"]                                  - Emit summary statistics instead of CSV
  //        ["device" <vendor id>:<device id>]           - Select a specific Vulkan Physical Device
  //        ["fill" <count> ["bytes"|"kbytes"|"mbytes"]  - Benchmark vkCmdFill()
  //                        ["fence"|"queue"|"timeline"]
  //                        ["repetitions" <count>]
  //                        ["warmup" <count>]
  //        ["copy" <count> ["bytes"|"kbytes"|"mbytes"]  - Benchmark vkCmdCopy()
  //                        ["fence"|"queue"|"timeline"]
  //                        ["repetitions" <count>]
  //                        ["warmup" <count>]
  //        ["noop"         ["fence"|"queue"|"timeline"]
  //                        ["repetitions" <count>]      - Benchmark "noop" compute pipeline
  //                        ["warmup" <count>]
  //
  fprintf(
    stderr, /* These are properly spaced to line up! */
    "Usage: %s\n"
    "       [\"quiet\"]                                    - Only print errors\n"
    "       [\"validate\"]                                 - Enable Vulkan Validation Layers\n"
    "       [\"summary\"]                                  - Emit summary statistics instead of CSV\n"
    "       [\"device\" <vendor id>:<device id>]           - Select a specific Vulkan Physical Device\n"
    "       [\"fill\" <count> [\"bytes\"|\"kbytes\"|\"mbytes\"]  - Benchmark vkCmdFill()\n"
    "                       [\"fence\"|\"queue\"|\"timeline\"]\n"
    "                       [\"repetitions\" <count>]\n"
    "                       [\"warmup\" <count>]\n"
    "       [\"copy\" <count> [\"bytes\"|\"kbytes\"|\"mbytes\"]  - Benchmark vkCmdCopy()\n"
    "                       [\"fence\"|\"queue\"|\"timeline\"]\n"
    "                       [\"repetitions\" <count>]\n"
    "                       [\"warmup\" <count>]\n"
    "       [\"noop\"         [\"fence\"|\"queue\"|\"timeline\"] - Benchmark compute pipeline\n"
    "                       [\"repetitions\" <count>]\n"
    "                       [\"warmup\" <count>]\n",
    argv[0]);
}

//
//
//
static void
bench_execute_fence(VkCommandBuffer                        cb,
                    struct bench_config const * const      config,
                    struct bench_config_iter const * const iter,
                    struct bench_split * const             splits)
{
  VkFenceCreateInfo const fci = {

    .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
    .pNext = NULL,
    .flags = 0
  };

  VkFence fence;

  vk(CreateFence(config->vk.d, &fci, NULL, &fence));

  VkSubmitInfo const si = {

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

  for (uint32_t ii = -iter->warmup; ii != iter->repetitions; ii++)
    {
      //
      // Submit
      //
      bool is_not_warmup = (ii < iter->repetitions);

      if (is_not_warmup)
        {
          bench_timestamp(&splits[ii].host.start);
        }

      vk(QueueSubmit(config->vk.q, 1, &si, fence));

      vk(WaitForFences(config->vk.d, 1, &fence, VK_TRUE, UINT64_MAX));

      if (is_not_warmup)
        {
          bench_timestamp(&splits[ii].host.stop);

          vk(GetQueryPoolResults(config->vk.d,
                                 config->vk.qp,
                                 0,
                                 2,
                                 sizeof(uint64_t) * 2,
                                 splits + ii,
                                 sizeof(uint64_t),
                                 VK_QUERY_RESULT_64_BIT | VK_QUERY_RESULT_WAIT_BIT));
        }

      vk(ResetFences(config->vk.d, 1, &fence));
    }

  vkDestroyFence(config->vk.d, fence, NULL);
}

//
//
//
static void
bench_execute_queue(VkCommandBuffer                        cb,
                    struct bench_config const * const      config,
                    struct bench_config_iter const * const iter,
                    struct bench_split * const             splits)
{
  VkSubmitInfo const si = {

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

  for (uint32_t ii = -iter->warmup; ii != iter->repetitions; ii++)
    {
      //
      // Submit
      //
      bool is_not_warmup = (ii < iter->repetitions);

      if (is_not_warmup)
        {
          bench_timestamp(&splits[ii].host.start);
        }

      vk(QueueSubmit(config->vk.q, 1, &si, VK_NULL_HANDLE));

      vk(QueueWaitIdle(config->vk.q));

      if (is_not_warmup)
        {
          bench_timestamp(&splits[ii].host.stop);

          vk(GetQueryPoolResults(config->vk.d,
                                 config->vk.qp,
                                 0,
                                 2,
                                 sizeof(uint64_t) * 2,
                                 splits + ii,
                                 sizeof(uint64_t),
                                 VK_QUERY_RESULT_64_BIT | VK_QUERY_RESULT_WAIT_BIT));
        }
    }
}

//
//
//
static void
bench_execute_timeline(VkCommandBuffer                        cb,
                       struct bench_config const * const      config,
                       struct bench_config_iter const * const iter,
                       struct bench_split * const             splits)
{
  VkSemaphoreTypeCreateInfo const stci = {

    .sType         = VK_STRUCTURE_TYPE_SEMAPHORE_TYPE_CREATE_INFO,
    .pNext         = NULL,
    .semaphoreType = VK_SEMAPHORE_TYPE_TIMELINE,
    .initialValue  = 0UL
  };

  VkSemaphoreCreateInfo const sci = {

    .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO,
    .pNext = &stci,
    .flags = 0
  };

  VkSemaphore semaphore;

  vk(CreateSemaphore(config->vk.d, &sci, NULL, &semaphore));

  uint64_t ssv[1] = { 0UL };  // increment this each iteration

  VkTimelineSemaphoreSubmitInfo const tssi = {

    .sType                     = VK_STRUCTURE_TYPE_TIMELINE_SEMAPHORE_SUBMIT_INFO,
    .pNext                     = NULL,
    .waitSemaphoreValueCount   = 0,
    .pWaitSemaphoreValues      = NULL,
    .signalSemaphoreValueCount = 1,
    .pSignalSemaphoreValues    = ssv
  };

  VkSubmitInfo const si = {

    .sType                = VK_STRUCTURE_TYPE_SUBMIT_INFO,
    .pNext                = &tssi,
    .waitSemaphoreCount   = 0,
    .pWaitSemaphores      = NULL,
    .pWaitDstStageMask    = NULL,
    .commandBufferCount   = 1,
    .pCommandBuffers      = &cb,
    .signalSemaphoreCount = 1,
    .pSignalSemaphores    = &semaphore
  };

  VkSemaphoreWaitInfo const swi = {

    .sType          = VK_STRUCTURE_TYPE_SEMAPHORE_WAIT_INFO,
    .pNext          = NULL,
    .flags          = 0,  // VK_SEMAPHORE_WAIT_ALL_BIT
    .semaphoreCount = 1,
    .pSemaphores    = &semaphore,
    .pValues        = ssv
  };

  for (uint32_t ii = -iter->warmup; ii != iter->repetitions; ii++)
    {
      //
      // Submit
      //
      bool is_not_warmup = (ii < iter->repetitions);

      if (is_not_warmup)
        {
          bench_timestamp(&splits[ii].host.start);
        }

      ssv[0] += 1UL;  // increment timeline signal

      vk(QueueSubmit(config->vk.q, 1, &si, VK_NULL_HANDLE));

      vk(WaitSemaphores(config->vk.d, &swi, UINT64_MAX));

      if (is_not_warmup)
        {
          bench_timestamp(&splits[ii].host.stop);

          vk(GetQueryPoolResults(config->vk.d,
                                 config->vk.qp,
                                 0,
                                 2,
                                 sizeof(uint64_t) * 2,
                                 splits + ii,
                                 sizeof(uint64_t),
                                 VK_QUERY_RESULT_64_BIT | VK_QUERY_RESULT_WAIT_BIT));
        }
    }

  vkDestroySemaphore(config->vk.d, semaphore, NULL);
}

//
//
//
static void
bench_execute(VkCommandBuffer                        cb,
              struct bench_config const * const      config,
              char const * const                     name,
              VkDeviceSize const                     bytes,
              struct bench_config_iter const * const iter,
              bench_wait_e const                     wait)
{
  //
  // Capture array of split times
  //
  struct bench_split * const splits = malloc(sizeof(*splits) * iter->repetitions);

  switch (wait)
    {
      case BENCH_WAIT_FENCE:
        bench_execute_fence(cb, config, iter, splits);
        break;

      case BENCH_WAIT_QUEUE:
        bench_execute_queue(cb, config, iter, splits);
        break;

      case BENCH_WAIT_TIMELINE:
        bench_execute_timeline(cb, config, iter, splits);
        break;
    }

  //
  // Report
  //
  bench_statistics(config, name, bytes, iter, wait, splits);

  //
  // Free splits
  //
  free(splits);
}

//
// Fill buffer A
//
static void
bench_fill(struct bench_config const * config, struct bench_config_fill * config_fill)
{
  // anything to do?
  if (config_fill->iter.repetitions == 0)
    {
      return;
    }

  //
  // Round up size to a dword
  //
  VkDeviceSize const fill_bytes    = (1UL << (10u * config_fill->unit)) * config_fill->count;
  VkDeviceSize const fill_bytes_ru = (fill_bytes + 3) & ~3UL;

  //
  // Allocate a single buffer
  //
  VkBufferCreateInfo const bci = {

    .sType                 = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
    .pNext                 = NULL,
    .flags                 = 0,
    .size                  = fill_bytes_ru,
    .usage                 = VK_BUFFER_USAGE_TRANSFER_DST_BIT,
    .sharingMode           = VK_SHARING_MODE_EXCLUSIVE,
    .queueFamilyIndexCount = 0,
    .pQueueFamilyIndices   = NULL
  };

  VkBuffer buffer_a;

  vk(CreateBuffer(config->vk.d, &bci, NULL, &buffer_a));

  VkMemoryRequirements mr_a;

  vkGetBufferMemoryRequirements(config->vk.d, buffer_a, &mr_a);

  VkMemoryAllocateInfo const mai_a = {

    .sType           = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
    .pNext           = NULL,
    .allocationSize  = mr_a.size,
    .memoryTypeIndex = vk_find_mem_type_idx(&config->vk.pdmp,  //
                                            mr_a.memoryTypeBits,
                                            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT)
  };

  VkDeviceMemory dm_a;

  vk(AllocateMemory(config->vk.d, &mai_a, NULL, &dm_a));

  vk(BindBufferMemory(config->vk.d, buffer_a, dm_a, 0));

  //
  // Allocate command buffer
  //
  VkCommandBufferAllocateInfo const cbai = {

    .sType              = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
    .pNext              = NULL,
    .commandPool        = config->vk.cp,
    .level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
    .commandBufferCount = 1
  };

  VkCommandBuffer cb;

  vk(AllocateCommandBuffers(config->vk.d, &cbai, &cb));

  //
  // Append commands to cb
  //
  VkCommandBufferBeginInfo const cbbi = {

    .sType            = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
    .pNext            = NULL,
    .flags            = 0,
    .pInheritanceInfo = NULL
  };

  vkBeginCommandBuffer(cb, &cbbi);

  //
  // Label begin
  //
  if (pfn_vkCmdBeginDebugUtilsLabelEXT != NULL)
    {
      VkDebugUtilsLabelEXT const label = {
        .sType      = VK_STRUCTURE_TYPE_DEBUG_UTILS_LABEL_EXT,
        .pNext      = NULL,
        .pLabelName = "vk_bench::fill",
      };

      pfn_vkCmdBeginDebugUtilsLabelEXT(cb, &label);
    }

  vkCmdResetQueryPool(cb, config->vk.qp, 0, 2);

  vkCmdWriteTimestamp(cb, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, config->vk.qp, 0);

  vkCmdFillBuffer(cb, buffer_a, 0UL, fill_bytes_ru, config_fill->value);

  vkCmdWriteTimestamp(cb, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, config->vk.qp, 1);

  //
  // Label end
  //
  if (pfn_vkCmdEndDebugUtilsLabelEXT != NULL)
    {
      pfn_vkCmdEndDebugUtilsLabelEXT(cb);
    }

  vk(EndCommandBuffer(cb));

  //
  // Execute and report
  //
  bench_execute(cb, config, "FILL", fill_bytes_ru, &config_fill->iter, config_fill->wait);

  //
  // Cleanup device
  //
  vkFreeCommandBuffers(config->vk.d, config->vk.cp, 1, &cb);
  vkDestroyBuffer(config->vk.d, buffer_a, NULL);
  vkFreeMemory(config->vk.d, dm_a, NULL);
}

//
// Copy buffer A to B
//
static void
bench_copy(struct bench_config const * config, struct bench_config_copy * config_copy)
{
  // anything to do?
  if (config_copy->iter.repetitions == 0)
    {
      return;
    }

  //
  // Round up size to a dword
  //
  VkDeviceSize const copy_bytes    = (1UL << (10u * config_copy->unit)) * config_copy->count;
  VkDeviceSize const copy_bytes_ru = (copy_bytes + 3) & ~3UL;

  //
  // Allocate a single buffer
  //
  VkBufferCreateInfo const bcis[2] = {

    { .sType                 = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
      .pNext                 = NULL,
      .flags                 = 0,
      .size                  = copy_bytes_ru,
      .usage                 = VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
      .sharingMode           = VK_SHARING_MODE_EXCLUSIVE,
      .queueFamilyIndexCount = 0,
      .pQueueFamilyIndices   = NULL },

    { .sType                 = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
      .pNext                 = NULL,
      .flags                 = 0,
      .size                  = copy_bytes_ru,
      .usage                 = VK_BUFFER_USAGE_TRANSFER_DST_BIT,
      .sharingMode           = VK_SHARING_MODE_EXCLUSIVE,
      .queueFamilyIndexCount = 0,
      .pQueueFamilyIndices   = NULL }
  };

  VkBuffer buffers[2];

  vk(CreateBuffer(config->vk.d, bcis + 0, NULL, buffers + 0));
  vk(CreateBuffer(config->vk.d, bcis + 1, NULL, buffers + 1));

  VkMemoryRequirements mrs[2];

  vkGetBufferMemoryRequirements(config->vk.d, buffers[0], mrs + 0);
  vkGetBufferMemoryRequirements(config->vk.d, buffers[1], mrs + 1);

  VkMemoryAllocateInfo const mais[2] = {

    { .sType           = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
      .pNext           = NULL,
      .allocationSize  = mrs[0].size,
      .memoryTypeIndex = vk_find_mem_type_idx(&config->vk.pdmp,  //
                                              mrs[0].memoryTypeBits,
                                              VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT) },

    { .sType           = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
      .pNext           = NULL,
      .allocationSize  = mrs[1].size,
      .memoryTypeIndex = vk_find_mem_type_idx(&config->vk.pdmp,  //
                                              mrs[1].memoryTypeBits,
                                              VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT) }
  };

  VkDeviceMemory dms[2];

  vk(AllocateMemory(config->vk.d, mais + 0, NULL, dms + 0));
  vk(AllocateMemory(config->vk.d, mais + 1, NULL, dms + 1));

  vk(BindBufferMemory(config->vk.d, buffers[0], dms[0], 0));
  vk(BindBufferMemory(config->vk.d, buffers[1], dms[1], 0));

  //
  // Allocate command buffer
  //
  VkCommandBufferAllocateInfo const cbai = {

    .sType              = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
    .pNext              = NULL,
    .commandPool        = config->vk.cp,
    .level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
    .commandBufferCount = 1
  };

  VkCommandBuffer cb;

  vk(AllocateCommandBuffers(config->vk.d, &cbai, &cb));

  //
  // Append commands to cb
  //
  VkCommandBufferBeginInfo const cbbi = {

    .sType            = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
    .pNext            = NULL,
    .flags            = 0,
    .pInheritanceInfo = NULL
  };

  vkBeginCommandBuffer(cb, &cbbi);

  //
  // Label begin
  //
  if (pfn_vkCmdBeginDebugUtilsLabelEXT != NULL)
    {
      VkDebugUtilsLabelEXT const label = {
        .sType      = VK_STRUCTURE_TYPE_DEBUG_UTILS_LABEL_EXT,
        .pNext      = NULL,
        .pLabelName = "vk_bench::copy",
      };

      pfn_vkCmdBeginDebugUtilsLabelEXT(cb, &label);
    }

  vkCmdResetQueryPool(cb, config->vk.qp, 0, 2);

  vkCmdWriteTimestamp(cb, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, config->vk.qp, 0);

  VkBufferCopy const bc = { .srcOffset = 0, .dstOffset = 0, .size = copy_bytes_ru };

  vkCmdCopyBuffer(cb, buffers[0], buffers[1], 1, &bc);

  vkCmdWriteTimestamp(cb, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, config->vk.qp, 1);

  //
  // Label end
  //
  if (pfn_vkCmdEndDebugUtilsLabelEXT != NULL)
    {
      pfn_vkCmdEndDebugUtilsLabelEXT(cb);
    }

  vk(EndCommandBuffer(cb));

  //
  // Execute and report
  //
  bench_execute(cb, config, "COPY", copy_bytes_ru, &config_copy->iter, config_copy->wait);

  //
  // Cleanup device
  //
  vkFreeCommandBuffers(config->vk.d, config->vk.cp, 1, &cb);
  vkDestroyBuffer(config->vk.d, buffers[0], NULL);
  vkDestroyBuffer(config->vk.d, buffers[1], NULL);
  vkFreeMemory(config->vk.d, dms[0], NULL);
  vkFreeMemory(config->vk.d, dms[1], NULL);
}

//
// Fill buffer A
//
static void
bench_noop(struct bench_config const * config, struct bench_config_noop * config_noop)
{
  // anything to do?
  if (config_noop->iter.repetitions == 0)
    {
      return;
    }

  //
  // Verify target archive magic
  //
  if (spirv_modules_rodata[0].magic != TARGET_ARCHIVE_MAGIC)
    {
      fprintf(stderr, "Error: Invalid target -- missing magic.");
    }

  //
  // Index into target archive data
  //
  struct target_archive_entry const * const ar_entries = spirv_modules_rodata[0].entries;

  uint32_t const * const ar_data = ar_entries[spirv_modules_rodata[0].count - 1].data;

  //
  // Create pipeline
  //
  VkPipelineLayoutCreateInfo const plci = {

    .sType                  = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
    .pNext                  = NULL,
    .flags                  = 0,
    .setLayoutCount         = 0,
    .pSetLayouts            = NULL,
    .pushConstantRangeCount = 0,
  };

  VkPipelineLayout pl;

  vk(CreatePipelineLayout(config->vk.d, &plci, NULL, &pl));

  VkShaderModuleCreateInfo const smci = {

    .sType    = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
    .pNext    = NULL,
    .flags    = 0,
    .codeSize = ar_entries[0].size,
    .pCode    = ar_data
  };

  VkShaderModule sm;

  vk(CreateShaderModule(config->vk.d, &smci, NULL, &sm));

  VkComputePipelineCreateInfo const cpci = {

    .sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,
    .pNext = NULL,
    .flags = 0,
    .stage = { .sType               = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
               .pNext               = NULL,
               .flags               = 0,
               .stage               = VK_SHADER_STAGE_COMPUTE_BIT,
               .module              = sm,
               .pName               = "main",
               .pSpecializationInfo = NULL },

    .layout             = pl,
    .basePipelineHandle = VK_NULL_HANDLE,
    .basePipelineIndex  = 0
  };

  VkPipeline p;

  // NOTE(allanmac): Skip creating a pipeline cache since it's a noop pipeline
  vk(CreateComputePipelines(config->vk.d, VK_NULL_HANDLE, 1, &cpci, NULL, &p));

  vkDestroyShaderModule(config->vk.d, sm, NULL);

  if (pfn_vkSetDebugUtilsObjectNameEXT != NULL)
    {
      VkDebugUtilsObjectNameInfoEXT const name = {
        .sType        = VK_STRUCTURE_TYPE_DEBUG_UTILS_OBJECT_NAME_INFO_EXT,
        .pNext        = NULL,
        .objectType   = VK_OBJECT_TYPE_PIPELINE,
        .objectHandle = (uint64_t)p,
        .pObjectName  = "bench-vk::noop"
      };

      vk_ok(pfn_vkSetDebugUtilsObjectNameEXT(config->vk.d, &name));
    }

  //
  // Allocate command buffer
  //
  VkCommandBufferAllocateInfo const cbai = {

    .sType              = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
    .pNext              = NULL,
    .commandPool        = config->vk.cp,
    .level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
    .commandBufferCount = 1
  };

  VkCommandBuffer cb;

  vk(AllocateCommandBuffers(config->vk.d, &cbai, &cb));

  //
  // Append commands to cb
  //
  VkCommandBufferBeginInfo const cbbi = {

    .sType            = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
    .pNext            = NULL,
    .flags            = 0,
    .pInheritanceInfo = NULL
  };

  vkBeginCommandBuffer(cb, &cbbi);

  //
  // Label begin
  //
  if (pfn_vkCmdBeginDebugUtilsLabelEXT != NULL)
    {
      VkDebugUtilsLabelEXT const label = {
        .sType      = VK_STRUCTURE_TYPE_DEBUG_UTILS_LABEL_EXT,
        .pNext      = NULL,
        .pLabelName = "vk_bench::noop",
      };

      pfn_vkCmdBeginDebugUtilsLabelEXT(cb, &label);
    }

  vkCmdResetQueryPool(cb, config->vk.qp, 0, 2);

  vkCmdWriteTimestamp(cb, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, config->vk.qp, 0);

  vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_COMPUTE, p);
  vkCmdDispatch(cb, 1, 1, 1);

  vkCmdWriteTimestamp(cb, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, config->vk.qp, 1);

  //
  // Label end
  //
  if (pfn_vkCmdEndDebugUtilsLabelEXT != NULL)
    {
      pfn_vkCmdEndDebugUtilsLabelEXT(cb);
    }

  vk(EndCommandBuffer(cb));

  //
  // Execute and report
  //
  bench_execute(cb, config, "NOOP", 0, &config_noop->iter, config_noop->wait);

  //
  // Cleanup pipeline objects
  //
  vkDestroyPipeline(config->vk.d, p, NULL);
  vkDestroyPipelineLayout(config->vk.d, pl, NULL);
}

//
//
//
static bool
bench_config_keyword_flag(uint32_t const     argc,
                          char const * const argv[],
                          uint32_t * const   next_token,
                          char const * const keyword,
                          bool * const       flag)
{
  uint32_t const next_token_start = *next_token;

  if ((*next_token < argc) && (strcmp(argv[*next_token], keyword) == 0))
    {
      ++*next_token;
      *flag = true;
    }

  return (*next_token > next_token_start);
}

//
//
//
static bool
bench_config_quiet(uint32_t const              argc,
                   char const * const          argv[],
                   uint32_t * const            next_token,
                   struct bench_config * const config)
{
  return bench_config_keyword_flag(argc, argv, next_token, "quiet", &config->is_quiet);
}

//
//
//
static bool
bench_config_validate(uint32_t const              argc,
                      char const * const          argv[],
                      uint32_t * const            next_token,
                      struct bench_config * const config)
{
  return bench_config_keyword_flag(argc, argv, next_token, "validate", &config->is_validate);
}

//
//
//
static bool
bench_config_summary(uint32_t const              argc,
                     char const * const          argv[],
                     uint32_t * const            next_token,
                     struct bench_config * const config)
{
  return bench_config_keyword_flag(argc, argv, next_token, "summary", &config->is_summary);
}

//
//
//
static bool
bench_config_device(uint32_t const              argc,
                    char const * const          argv[],
                    uint32_t * const            next_token,
                    struct bench_config * const config)
{
  uint32_t const next_token_start = *next_token;

  // need at least 2 args
  if ((*next_token + 1 < argc) && (strcmp(argv[*next_token], "device") == 0))
    {
      ++*next_token;

      if ((*next_token < argc) && (sscanf(argv[*next_token],  //
                                          "%x:%x",
                                          &config->vendor_id,
                                          &config->device_id) == 2))
        {
          ++*next_token;
        }
      else
        {
          fprintf(stderr, "Error: expected: \"<vendor id>:<device id>\"\n");
        }
    }

  return (*next_token > next_token_start);
}

//
//
//
static bool
bench_config_unit(uint32_t const       argc,
                  char const * const   argv[],
                  uint32_t * const     next_token,
                  bench_unit_e * const unit)
{
  uint32_t const next_token_start = *next_token;

  if ((*next_token < argc) && (strcmp(argv[*next_token], "bytes") == 0))
    {
      *unit = BENCH_UNIT_BYTES;
      ++*next_token;
    }
  else if ((*next_token < argc) && (strcmp(argv[*next_token], "kbytes") == 0))
    {
      *unit = BENCH_UNIT_KBYTES;
      ++*next_token;
    }
  else if ((*next_token < argc) && (strcmp(argv[*next_token], "mbytes") == 0))
    {
      *unit = BENCH_UNIT_MBYTES;
      ++*next_token;
    }
  else if ((*next_token < argc) && (strcmp(argv[*next_token], "gbytes") == 0))
    {
      *unit = BENCH_UNIT_GBYTES;
      ++*next_token;
    }

  return (*next_token > next_token_start);
}

//
//
//
static bool
bench_config_wait(uint32_t const       argc,
                  char const * const   argv[],
                  uint32_t * const     next_token,
                  bench_wait_e * const wait)
{
  uint32_t const next_token_start = *next_token;

  if ((*next_token < argc) && (strcmp(argv[*next_token], "fence") == 0))
    {
      *wait = BENCH_WAIT_FENCE;
      ++*next_token;
    }
  else if ((*next_token < argc) && (strcmp(argv[*next_token], "queue") == 0))
    {
      *wait = BENCH_WAIT_QUEUE;
      ++*next_token;
    }
  else if ((*next_token < argc) && (strcmp(argv[*next_token], "timeline") == 0))
    {
      *wait = BENCH_WAIT_TIMELINE;
      ++*next_token;
    }

  return (*next_token > next_token_start);
}

//
//
//
static bool
bench_config_keyword_uint32(uint32_t           argc,
                            char const * const argv[],
                            uint32_t * const   next_token,
                            char const * const keyword,
                            uint32_t * const   value)
{
  uint32_t const next_token_start = *next_token;

  if ((*next_token + 1 < argc) && (strcmp(argv[*next_token], keyword) == 0))
    {
      ++*next_token;

      char * str_end;

      *value = (uint32_t)strtoul(argv[*next_token], &str_end, 0);

      if (str_end == argv[*next_token])  // error
        {
          return *next_token;
        }

      ++*next_token;
    }

  return (*next_token > next_token_start);
}

//
//
//
static bool
bench_config_repetitions(uint32_t           argc,
                         char const * const argv[],
                         uint32_t * const   next_token,
                         uint32_t * const   repetitions)
{
  return bench_config_keyword_uint32(argc, argv, next_token, "repetitions", repetitions);
}

//
//
//
static bool
bench_config_warmup(uint32_t const     argc,
                    char const * const argv[],
                    uint32_t * const   next_token,
                    uint32_t * const   warmup)
{
  return bench_config_keyword_uint32(argc, argv, next_token, "warmup", warmup);
}

//
//
//
static bool
bench_config_iter(uint32_t const                   argc,
                  char const * const               argv[],
                  uint32_t * const                 next_token,
                  struct bench_config_iter * const config_iter)
{
  uint32_t const next_token_start = *next_token;

  while (bench_config_repetitions(argc, argv, next_token, &config_iter->repetitions) ||
         bench_config_warmup(argc, argv, next_token, &config_iter->warmup))
    {
      ;
    }

  return (*next_token != next_token_start);
}

//
//
//
static bool
bench_config_fill(uint32_t const                   argc,
                  char const * const               argv[],
                  uint32_t * const                 next_token,
                  struct bench_config_fill * const config_fill)
{
  uint32_t const next_token_start = *next_token;

  if (bench_config_keyword_uint32(argc, argv, next_token, "fill", &config_fill->count))
    {
      // defaults
      config_fill->value = 0xBAADF00D;
      config_fill->count = BENCH_CONFIG_DEFAULT_COUNT;
      config_fill->iter  = (struct bench_config_iter){

        .repetitions = BENCH_CONFIG_DEFAULT_REPETITIONS,
        .warmup      = BENCH_CONFIG_DEFAULT_WARMUP
      };

      while (bench_config_unit(argc, argv, next_token, &config_fill->unit) ||
             bench_config_wait(argc, argv, next_token, &config_fill->wait) ||
             bench_config_iter(argc, argv, next_token, &config_fill->iter))
        {
          ;
        }
    }

  return (*next_token != next_token_start);
}

//
//
//
static bool
bench_config_copy(uint32_t const                   argc,
                  char const * const               argv[],
                  uint32_t * const                 next_token,
                  struct bench_config_copy * const config_copy)
{
  uint32_t const next_token_start = *next_token;

  if (bench_config_keyword_uint32(argc, argv, next_token, "copy", &config_copy->count))
    {
      // defaults
      config_copy->count = BENCH_CONFIG_DEFAULT_COUNT;
      config_copy->iter  = (struct bench_config_iter){

        .repetitions = BENCH_CONFIG_DEFAULT_REPETITIONS,
        .warmup      = BENCH_CONFIG_DEFAULT_WARMUP
      };

      while (bench_config_unit(argc, argv, next_token, &config_copy->unit) ||
             bench_config_wait(argc, argv, next_token, &config_copy->wait) ||
             bench_config_iter(argc, argv, next_token, &config_copy->iter))
        {
          ;
        }
    }

  return (*next_token != next_token_start);
}

//
//
//
static uint32_t
bench_config_noop(uint32_t const                   argc,
                  char const * const               argv[],
                  uint32_t * const                 next_token,
                  struct bench_config_noop * const config_noop)
{
  uint32_t const next_token_start = *next_token;

  if ((*next_token < argc) && (strcmp(argv[*next_token], "noop") == 0))
    {
      ++*next_token;

      // defaults
      config_noop->iter = (struct bench_config_iter){

        .repetitions = BENCH_CONFIG_DEFAULT_REPETITIONS,
        .warmup      = BENCH_CONFIG_DEFAULT_WARMUP
      };

      while (bench_config_iter(argc, argv, next_token, &config_noop->iter) ||
             bench_config_wait(argc, argv, next_token, &config_noop->wait))
        {
          ;
        }
    }

  return (*next_token != next_token_start);
}

//
// Require at least one command
//
int
bench_vk(uint32_t argc, char const * argv[])
{
  struct bench_config      config      = { 0 };
  struct bench_config_fill config_fill = { 0 };
  struct bench_config_copy config_copy = { 0 };
  struct bench_config_noop config_noop = { 0 };

  if (argc == 1)
    {
      config_copy = (struct bench_config_copy){

        .count = BENCH_CONFIG_DEFAULT_COUNT,
        .iter  = { .repetitions = BENCH_CONFIG_DEFAULT_REPETITIONS,
                   .warmup      = BENCH_CONFIG_DEFAULT_WARMUP },
      };

      fprintf(stderr,
              "\n"
              "No commands. Benchmarking \"vkCmdCopyBuffer()\" on first Vulkan device.\n"
              "\n");
    }

  //
  // Greedily consume tokens
  //
  uint32_t next_token = 1;

  while (bench_config_quiet(argc, argv, &next_token, &config) ||
         bench_config_validate(argc, argv, &next_token, &config) ||
         bench_config_summary(argc, argv, &next_token, &config) ||
         bench_config_device(argc, argv, &next_token, &config) ||
         bench_config_fill(argc, argv, &next_token, &config_fill) ||
         bench_config_copy(argc, argv, &next_token, &config_copy) ||
         bench_config_noop(argc, argv, &next_token, &config_noop))
    {
      ;
    }

  if (next_token < argc)
    {
      fprintf(stderr, "Error: unexpected arguments: ");

      for (uint32_t ii = next_token; ii < argc; ii++)
        {
          fprintf(stderr, "%s ", argv[ii]);
        }

      fprintf(stderr, "\n");

      fprintf(stderr, "                             ^\n");

      return EXIT_FAILURE;
    }

  //
  // Prepare Vulkan environment
  //
  VkApplicationInfo const app_info = {

    .sType              = VK_STRUCTURE_TYPE_APPLICATION_INFO,
    .pNext              = NULL,
    .pApplicationName   = "bench-vk",
    .applicationVersion = 0,
    .pEngineName        = "bench-vk",
    .engineVersion      = 0,
    .apiVersion         = VK_API_VERSION_1_2
  };

  char const * const inst_layers[]     = { "VK_LAYER_KHRONOS_validation" /* must be last */ };
  char const * const inst_extensions[] = { VK_EXT_DEBUG_UTILS_EXTENSION_NAME };

  uint32_t const inst_layer_count     = ARRAY_LENGTH_MACRO(inst_layers);
  uint32_t const inst_extension_count = ARRAY_LENGTH_MACRO(inst_extensions);

  VkInstanceCreateInfo const instance_info = {

    .sType                   = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
    .pNext                   = NULL,
    .flags                   = 0,
    .pApplicationInfo        = &app_info,
    .enabledLayerCount       = config.is_validate ? inst_layer_count : inst_layer_count - 1,
    .ppEnabledLayerNames     = inst_layers,
    .enabledExtensionCount   = inst_extension_count,
    .ppEnabledExtensionNames = inst_extensions
  };

  vk(CreateInstance(&instance_info, NULL, &config.vk.i));

  //
  // init debug utils
  //
  vk_debug_utils_init(config.vk.i);

  //
  // acquire all physical devices
  //
  uint32_t pd_count;

  vk(EnumeratePhysicalDevices(config.vk.i, &pd_count, NULL));

  VkPhysicalDevice * pds = malloc(pd_count * sizeof(*pds));

  vk(EnumeratePhysicalDevices(config.vk.i, &pd_count, pds));

  //
  // if no vendor devicedefault to first physical device
  //
  if ((config.vendor_id == 0) && (config.device_id == 0))
    {
      VkPhysicalDeviceProperties tmp;

      vkGetPhysicalDeviceProperties(pds[0], &tmp);

      config.vendor_id = tmp.vendorID;
      config.device_id = tmp.deviceID;
    }

  //
  // find a matching device
  //
  for (uint32_t ii = 0; ii < pd_count; ii++)
    {
      VkPhysicalDeviceProperties pdp;

      vkGetPhysicalDeviceProperties(pds[ii], &pdp);

      bool const is_match = (pdp.vendorID == config.vendor_id) &&  //
                            (pdp.deviceID == config.device_id);

      if (!config.is_quiet)
        {
          fprintf(stdout,
                  "%c %8X : %-8X : %s\n",
                  is_match ? '*' : ' ',
                  pdp.vendorID,
                  pdp.deviceID,
                  pdp.deviceName);
        }

      if (is_match)
        {
          config.vk.pd  = pds[ii];
          config.vk.pdp = pdp;
        }
    }

  if (!config.is_quiet)
    {
      fprintf(stdout, "\n");
    }

  if (config.vk.pd == VK_NULL_HANDLE)
    {
      fprintf(stderr, "Error: Device %4X:%X not found\n", config.vendor_id, config.device_id);
      return EXIT_FAILURE;
    }

  free(pds);

  //
  // timestamp support is required
  //
  if (config.vk.pdp.limits.timestampComputeAndGraphics != VK_TRUE)
    {
      fprintf(stderr, "Error: Vulkan `pdp.limits.timestampComputeAndGraphics != VK_TRUE`\n");
      return EXIT_FAILURE;
    }

  //
  // get the physical device's memory props
  //
  vkGetPhysicalDeviceMemoryProperties(config.vk.pd, &config.vk.pdmp);

  //
  // get queue properties for queue 0 to silence validation
  //
  uint32_t qfp_count;

  vkGetPhysicalDeviceQueueFamilyProperties(config.vk.pd, &qfp_count, NULL);

  VkQueueFamilyProperties qfp[1];

  qfp_count = 1;

  vkGetPhysicalDeviceQueueFamilyProperties(config.vk.pd, &qfp_count, qfp);

  //
  // one compute queue -- default to index 0
  //
  float const qci_priorities[] = { 1.0f };

  VkDeviceQueueCreateInfo const qci = {

    .sType            = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
    .pNext            = NULL,
    .flags            = 0,
    .queueFamilyIndex = BENCH_CONFIG_DEFAULT_QUEUE_FAMILY_INDEX,
    .queueCount       = 1,
    .pQueuePriorities = qci_priorities
  };

  //
  // feature structures chain
  //
  VkPhysicalDeviceVulkan12Features pdf12 = {
    .sType             = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES,
    .timelineSemaphore = true
  };

  VkPhysicalDeviceVulkan11Features pdf11 = {
    .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_FEATURES,
    .pNext = &pdf12
  };

  VkPhysicalDeviceFeatures2 pdf2 = { .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2,
                                     .pNext = &pdf11 };

//
// probe extensions for "VK_EXT_calibrated_timestamps" support
//
// #define VK_BENCH_DISABLE_CALIBRATED_TIMESTAMPS
#ifndef VK_BENCH_DISABLE_CALIBRATED_TIMESTAMPS
  uint32_t device_ext_count;

  vkEnumerateDeviceExtensionProperties(config.vk.pd, NULL, &device_ext_count, NULL);

  VkExtensionProperties * device_ext_props = malloc(sizeof(*device_ext_props) * device_ext_count);

  vkEnumerateDeviceExtensionProperties(config.vk.pd, NULL, &device_ext_count, device_ext_props);

  for (uint32_t ii = 0; ii < device_ext_count; ii++)
    {
      if (strcmp(device_ext_props[ii].extensionName,  //
                 VK_EXT_CALIBRATED_TIMESTAMPS_EXTENSION_NAME) == 0)
        {
          config.is_calibrated = true;
          break;
        }
    }

  free(device_ext_props);
#endif

  //
  // device extensions
  //
  char const *   ext_names[]    = { VK_EXT_CALIBRATED_TIMESTAMPS_EXTENSION_NAME };
  uint32_t const ext_name_count = ARRAY_LENGTH_MACRO(ext_names);

  //
  // create VkDevice
  //
  VkDeviceCreateInfo const dci = {

    .sType                   = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
    .pNext                   = &pdf2,
    .flags                   = 0,
    .queueCreateInfoCount    = 1,
    .pQueueCreateInfos       = &qci,
    .enabledLayerCount       = 0,
    .ppEnabledLayerNames     = NULL,
    .enabledExtensionCount   = config.is_calibrated ? ext_name_count : ext_name_count - 1,
    .ppEnabledExtensionNames = ext_names,
    .pEnabledFeatures        = NULL
  };

  vk(CreateDevice(config.vk.pd, &dci, NULL, &config.vk.d));

  //
  // get calibrated timestamps pfn -- will be NULL if extensions isn't enabled/present
  //
  pfn_vkGetCalibratedTimestampsEXT =
    (PFN_vkGetCalibratedTimestampsEXT)vkGetDeviceProcAddr(config.vk.d,
                                                          "vkGetCalibratedTimestampsEXT");
  //
  // get a queue
  //
  vkGetDeviceQueue(config.vk.d, 0, 0, &config.vk.q);

  //
  // create command pool
  //
  // - default to queue family index 0
  // - cmd bufs are resettable
  //
  VkCommandPoolCreateInfo const cpci = {

    .sType            = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
    .pNext            = NULL,
    .flags            = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,
    .queueFamilyIndex = BENCH_CONFIG_DEFAULT_QUEUE_FAMILY_INDEX
  };

  vk(CreateCommandPool(config.vk.d, &cpci, NULL, &config.vk.cp));

  //
  // create a query pool for benchmarking
  //
  VkQueryPoolCreateInfo const qpci = {

    .sType              = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO,
    .pNext              = NULL,
    .flags              = 0,
    .queryType          = VK_QUERY_TYPE_TIMESTAMP,
    .queryCount         = 2,
    .pipelineStatistics = 0
  };

  vk(CreateQueryPool(config.vk.d, &qpci, NULL, &config.vk.qp));

  //
  // Execute each benchmark
  //
  bench_fill(&config, &config_fill);
  bench_copy(&config, &config_copy);
  bench_noop(&config, &config_noop);

  //
  // Cleanup device
  //
  vkDestroyQueryPool(config.vk.d, config.vk.qp, NULL);
  vkDestroyCommandPool(config.vk.d, config.vk.cp, NULL);
  vkDestroyDevice(config.vk.d, NULL);
  vkDestroyInstance(config.vk.i, NULL);

  return EXIT_SUCCESS;
}

//
//
//
