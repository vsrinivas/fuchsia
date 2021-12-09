// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//
//
//

#include "deps.h"

#include <assert.h>
#include <stdlib.h>
#include <string.h>

#include "common/vk/assert.h"
#include "device.h"
#include "queue_pool.h"

//
// Utility struct and functions for accumulating a wait set from a bag of
// delayed semaphore indices.
//
#define SPN_DEPS_WAITSET_DELAYED_BITMAP_DWORDS ((SPN_DEPS_DELAYED_SEMAPHORE_MAX + 31) / 32)

struct spinel_deps_waitset_gather
{
  struct
  {
    uint32_t bitmap[SPN_DEPS_WAITSET_DELAYED_BITMAP_DWORDS];
  } delayed;
};

//
// clang-format off
//

//
// Stack allocated store of waiting semaphores totals:
//
//  * Every in-flight delayed semaphore
//  * In-flight immediate semaphores
//  * Internal transfer waiting timelines
//  * Imported waiting timelines
//
#define SPN_DEPS_WAITSET_SIZE (SPN_DEPS_DELAYED_SEMAPHORE_MAX                + \
                               SPN_DEPS_IMMEDIATE_SUBMIT_SIZE_WAIT_IMMEDIATE + \
                               SPN_DEPS_TRANSFER_WAIT_SIZE                   + \
                               SPN_VK_SEMAPHORE_IMPORT_WAIT_SIZE)

struct spinel_deps_waitset
{
  uint32_t             count;
  VkPipelineStageFlags stages    [SPN_DEPS_WAITSET_SIZE];
  VkSemaphore          semaphores[SPN_DEPS_WAITSET_SIZE];
  uint64_t             values    [SPN_DEPS_WAITSET_SIZE];
};

//
// Stack allocated store of signalling semaphores totals:
//
//  * Delayed signal semaphores
//  * One just-acquired immediate semaphore
//  * Internal transfer signalling timelines
//  * Imported signalling timelines
//
#define SPN_DEPS_SIGNALSET_SIZE (SPN_DEPS_IMMEDIATE_SUBMIT_SIZE_SIGNAL_DELAYED + \
                                 1                                             + \
                                 SPN_DEPS_TRANSFER_SIGNAL_SIZE                 + \
                                 SPN_VK_SEMAPHORE_IMPORT_SIGNAL_SIZE)

struct spinel_deps_signalset
{
  uint32_t    count;
  VkSemaphore semaphores[SPN_DEPS_SIGNALSET_SIZE];
  uint64_t    values    [SPN_DEPS_SIGNALSET_SIZE];
};

//
// clang-format on
//

//
// Deps instance
//
struct spinel_deps
{
  struct
  {
    struct
    {
      uint32_t        size;   // size of a pool
      uint32_t        count;  // number of pools in extent
      VkCommandPool * extent;
    } pool;

    uint32_t                    size;  // (pool.size * pool.count)
    uint32_t                    head;
    uint32_t                    tail;
    uint32_t                    submissions;
    VkPipelineStageFlags *      stages;
    VkSemaphore *               semaphores;
    uint64_t *                  values;
    VkCommandBuffer *           cbs;
    struct spinel_deps_action * completions;
  } immediate;

  struct
  {
    uint32_t                          size;
    uint32_t                          next;
    VkSemaphore *                     semaphores;
    uint64_t *                        values;
    struct spinel_deps_action *       submissions;
    spinel_deps_delayed_semaphore_t * handle_map;
  } delayed;
};

//
//
//
struct spinel_deps *
spinel_deps_create(struct spinel_deps_create_info const * info, struct spinel_device_vk const * vk)
{
  uint32_t const immediate_size = info->semaphores.immediate.pool.size *  //
                                  info->semaphores.immediate.pool.count;

  assert(immediate_size <= SPN_DEPS_IMMEDIATE_SEMAPHORE_MAX);
  assert(info->semaphores.delayed.size <= SPN_DEPS_DELAYED_SEMAPHORE_MAX);

  //
  // alloc deps
  //
  struct spinel_deps * deps = malloc(sizeof(*deps));

  //
  // constants and counters
  //
  deps->immediate.pool.size   = info->semaphores.immediate.pool.size;
  deps->immediate.pool.count  = info->semaphores.immediate.pool.count;
  deps->immediate.size        = immediate_size;
  deps->immediate.head        = 0;
  deps->immediate.tail        = 0;
  deps->immediate.submissions = 0;

  deps->delayed.size = info->semaphores.delayed.size;
  deps->delayed.next = 0;

  //
  // allocations
  //
  size_t const handle_map_size = info->handle_count * sizeof(*deps->delayed.handle_map);

  // clang-format off
  deps->immediate.pool.extent = malloc(info->semaphores.immediate.pool.count * sizeof(*deps->immediate.pool.extent));
  deps->immediate.stages      = malloc(immediate_size * sizeof(*deps->immediate.stages));
  deps->immediate.semaphores  = malloc(immediate_size * sizeof(*deps->immediate.semaphores));
  deps->immediate.values      = calloc(immediate_size, sizeof(*deps->immediate.values));
  deps->immediate.cbs         = malloc(immediate_size * sizeof(*deps->immediate.cbs));
  deps->immediate.completions = calloc(immediate_size, sizeof(*deps->immediate.completions));

  deps->delayed.semaphores    = malloc(info->semaphores.delayed.size * sizeof(*deps->delayed.semaphores));
  deps->delayed.values        = calloc(info->semaphores.delayed.size, sizeof(*deps->delayed.values));
  deps->delayed.submissions   = calloc(info->semaphores.delayed.size, sizeof(*deps->delayed.submissions));
  deps->delayed.handle_map    = malloc(handle_map_size);
  // clang-format on

  //
  // Invalidate handle map
  //
  memset(deps->delayed.handle_map, 0xFF, handle_map_size);

  //
  // Create command pool
  //
  VkCommandPoolCreateInfo const cpci = {

    .sType            = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
    .pNext            = NULL,
    .flags            = 0,
    .queueFamilyIndex = vk->q.compute.create_info.family_index
  };

  VkCommandBufferAllocateInfo cbai = {

    .sType              = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
    .pNext              = NULL,
    .level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
    .commandBufferCount = info->semaphores.immediate.pool.size,
    .commandPool        = VK_NULL_HANDLE  // updated on each iteration
  };

  for (uint32_t ii = 0; ii < deps->immediate.pool.count; ii++)
    {
      vk(CreateCommandPool(vk->d, &cpci, vk->ac, deps->immediate.pool.extent + ii));

      //
      // Allocate command buffers
      //
      uint32_t const cmds_base = info->semaphores.immediate.pool.size * ii;

      cbai.commandPool = deps->immediate.pool.extent[ii];

      vk(AllocateCommandBuffers(vk->d, &cbai, deps->immediate.cbs + cmds_base));
    }

  //
  // Create timeline semaphores
  //
  VkSemaphoreTypeCreateInfo const stci = {

    .sType         = VK_STRUCTURE_TYPE_SEMAPHORE_TYPE_CREATE_INFO_KHR,
    .pNext         = NULL,
    .semaphoreType = VK_SEMAPHORE_TYPE_TIMELINE,
    .initialValue  = 0UL
  };

  VkSemaphoreCreateInfo const sci = {

    .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO,
    .pNext = &stci,
    .flags = 0
  };

  //
  // Create immediate timeline semaphores initialized to 0
  //
  for (uint32_t ii = 0; ii < immediate_size; ii++)
    {
      vk(CreateSemaphore(vk->d, &sci, vk->ac, deps->immediate.semaphores + ii));
    }

  //
  // Create delayed timeline semaphores initialized to 0
  //
  for (uint32_t ii = 0; ii < info->semaphores.delayed.size; ii++)
    {
      vk(CreateSemaphore(vk->d, &sci, vk->ac, deps->delayed.semaphores + ii));
    }

  return deps;
}

//
//
//
void
spinel_deps_dispose(struct spinel_deps * deps, struct spinel_device_vk const * vk)
{
  //
  // Destroy semaphores
  //
  for (uint32_t ii = 0; ii < deps->immediate.size; ii++)
    {
      vkDestroySemaphore(vk->d, deps->immediate.semaphores[ii], vk->ac);
    }

  for (uint32_t ii = 0; ii < deps->delayed.size; ii++)
    {
      vkDestroySemaphore(vk->d, deps->delayed.semaphores[ii], vk->ac);
    }

  //
  // Free command buffers
  //
  for (uint32_t ii = 0; ii < deps->immediate.pool.count; ii++)
    {
      uint32_t const pool_base = ii * deps->immediate.pool.size;

      vkFreeCommandBuffers(vk->d,
                           deps->immediate.pool.extent[ii],
                           deps->immediate.pool.size,
                           deps->immediate.cbs + pool_base);
    }

  //
  // Destroy command pools
  //
  for (uint32_t ii = 0; ii < deps->immediate.pool.count; ii++)
    {
      vkDestroyCommandPool(vk->d, deps->immediate.pool.extent[ii], vk->ac);
    }

  //
  // Arrays
  //
  free(deps->immediate.pool.extent);
  free(deps->immediate.stages);
  free(deps->immediate.semaphores);
  free(deps->immediate.values);
  free(deps->immediate.cbs);
  free(deps->immediate.completions);

  free(deps->delayed.semaphores);
  free(deps->delayed.values);
  free(deps->delayed.submissions);
  free(deps->delayed.handle_map);

  free(deps);
}

//
// Attach a semaphore to a handle
//
void
spinel_deps_delayed_attach(struct spinel_deps *            deps,
                           spinel_handle_t                 handle,
                           spinel_deps_delayed_semaphore_t semaphore)
{
  deps->delayed.handle_map[handle] = semaphore;
}

//
// Detach a semaphore from an extent of handles
//
void
spinel_deps_delayed_detach(struct spinel_deps *    deps,
                           spinel_handle_t const * handles,
                           uint32_t                count)
{
  for (uint32_t ii = 0; ii < count; ii++)
    {
      spinel_handle_t const handle = handles[ii];

      deps->delayed.handle_map[handle] = SPN_DEPS_DELAYED_SEMAPHORE_INVALID;
    }
}

//
// Detach a semaphore from a ring of handles
//
void
spinel_deps_delayed_detach_ring(struct spinel_deps *    deps,
                                spinel_handle_t const * handles,
                                uint32_t                size,
                                uint32_t                head,
                                uint32_t                span)
{
  uint32_t const head_max   = head + span;
  uint32_t const head_clamp = MIN_MACRO(uint32_t, head_max, size);
  uint32_t const count_lo   = head_clamp - head;

  spinel_deps_delayed_detach(deps, handles + head, count_lo);

  if (span > count_lo)
    {
      uint32_t const count_hi = span - count_lo;

      spinel_deps_delayed_detach(deps, handles, count_hi);
    }
}

//
//
//
static void
spinel_deps_action_invoke(struct spinel_deps_action * action)
{
  if (action->pfn != NULL)
    {
      spinel_deps_pfn_t pfn = action->pfn;

      action->pfn = NULL;

      pfn(action->data0, action->data1);
    }
}

//
// Flush a delayed semaphore
//
void
spinel_deps_delayed_flush(struct spinel_deps * deps, spinel_deps_delayed_semaphore_t delayed)
{
  spinel_deps_action_invoke(deps->delayed.submissions + delayed);
}

//
// Gather the delayed semaphores of a linear span of handles
//
static void
spinel_deps_waitset_gather_init(struct spinel_deps const *          deps,
                                spinel_handle_t const *             handles,
                                uint32_t const                      count,
                                struct spinel_deps_waitset_gather * gather)
{
  for (uint32_t ii = 0; ii < count; ii++)
    {
      spinel_handle_t const                 handle  = handles[ii];
      spinel_deps_delayed_semaphore_t const delayed = deps->delayed.handle_map[handle];

      if (delayed != SPN_DEPS_DELAYED_SEMAPHORE_INVALID)
        {
          uint32_t const delayed_base = (delayed >> 5);
          uint32_t const delayed_bit  = (1u << (delayed & 0x1F));

          gather->delayed.bitmap[delayed_base] |= delayed_bit;
        }
    }
}

//
// 1. Gather delayed semaphores
// 2. For all delayed semaphores:
//    1. Invoke .submission action
//    2. Save semaphore handle and signalling value
//
static void
spinel_deps_waitset_init(struct spinel_deps const *                       deps,
                         struct spinel_deps_waitset *                     waitset,
                         struct spinel_deps_immediate_submit_info const * info)
{
  uint32_t wait_count = 0;

  //
  // First append info->wait.immediate[] array because we know the latest
  // (signal) value is valid.
  //
  for (uint32_t ii = 0; ii < info->wait.immediate.count; ii++)
    {
      spinel_deps_immediate_semaphore_t immediate = info->wait.immediate.semaphores[ii];

      waitset->stages[wait_count]     = deps->immediate.stages[immediate];
      waitset->semaphores[wait_count] = deps->immediate.semaphores[immediate];
      waitset->values[wait_count]     = deps->immediate.values[immediate];

      wait_count += 1;
    }

  //
  // Gather the delayed semaphores of a ring of handles
  //
  if (info->wait.delayed.handles.span > 0)
    {
      //
      // Gather bitmap of valid (in-flight) handles
      //
      struct spinel_deps_waitset_gather gather = { 0 };

      uint32_t const head_max   = info->wait.delayed.handles.head + info->wait.delayed.handles.span;
      uint32_t const head_clamp = MIN_MACRO(uint32_t, head_max, info->wait.delayed.handles.size);
      uint32_t       count_lo   = head_clamp - info->wait.delayed.handles.head;

      spinel_handle_t const * handle_head = info->wait.delayed.handles.extent +  //
                                            info->wait.delayed.handles.head;

      spinel_deps_waitset_gather_init(deps, handle_head, count_lo, &gather);

      if (info->wait.delayed.handles.span > count_lo)
        {
          uint32_t count_hi = info->wait.delayed.handles.span - count_lo;

          spinel_deps_waitset_gather_init(deps,
                                          info->wait.delayed.handles.extent,
                                          count_hi,
                                          &gather);
        }

      //
      // Dispatch each delayed semaphore and save the semaphore and its
      // signalling value
      //
      for (uint32_t ii = 0; ii < SPN_DEPS_WAITSET_DELAYED_BITMAP_DWORDS; ii++)
        {
          uint32_t dword = gather.delayed.bitmap[ii];

          if (dword == 0)
            {
              continue;
            }

          uint32_t delayed_base = ii * 32;

          do
            {
              uint32_t const lsb_plus_1 = __builtin_ffs(dword);  // [1,32]

              dword >>= lsb_plus_1;  // shift away bits

              delayed_base += lsb_plus_1;  // update delayed semaphore index

              uint32_t const delayed = delayed_base - 1;

              spinel_deps_action_invoke(deps->delayed.submissions + delayed);

              waitset->stages[wait_count]     = VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;
              waitset->semaphores[wait_count] = deps->delayed.semaphores[delayed];
              waitset->values[wait_count]     = deps->delayed.values[delayed];

              wait_count += 1;

          } while (dword != 0);
        }
    }

  waitset->count = wait_count;
}

//
//
//
static void
spinel_deps_waitset_append_transfer(struct spinel_deps const *                       deps,
                                    struct spinel_deps_waitset *                     waitset,
                                    struct spinel_deps_immediate_submit_info const * info)
{
  for (uint32_t ii = 0; ii < info->wait.transfer.count; ii++)
    {
      waitset->stages[waitset->count]     = info->wait.transfer.stages[ii];
      waitset->semaphores[waitset->count] = info->wait.transfer.semaphores[ii];
      waitset->values[waitset->count]     = info->wait.transfer.values[ii];

      waitset->count += 1;
    }
}

//
//
//
static void
spinel_deps_waitset_append_import(struct spinel_deps const *                       deps,
                                  struct spinel_deps_waitset *                     waitset,
                                  struct spinel_deps_immediate_submit_info const * info)
{
  for (uint32_t ii = 0; ii < info->wait.import.count; ii++)
    {
      waitset->stages[waitset->count]     = info->wait.import.stages[ii];
      waitset->semaphores[waitset->count] = info->wait.import.semaphores[ii];
      waitset->values[waitset->count]     = info->wait.import.values[ii];

      waitset->count += 1;
    }
}

//
// Drains the submission at deps->immediate.tail.
//
// NOTE: Assumes (deps->immediate.submissions > 0)
//
// FIXME(allanmac): Refactor to support VK_ERROR_DEVICE_LOST
//
static bool
spinel_deps_drain_tail(struct spinel_deps *            deps,
                       struct spinel_device_vk const * vk,
                       uint64_t                        timeout)
{
  //
  // Wait for this timeline to complete...
  //
  // NOTE(allanmac): This assumes the wait never times out.  If the device is
  // lost then we fail.  The proper way to handle this is to replace all context
  // pfns with device lost operations.
  //
  uint32_t const immediate = deps->immediate.tail;

  VkSemaphoreWaitInfo const swi = {
    .sType          = VK_STRUCTURE_TYPE_SEMAPHORE_WAIT_INFO,
    .pNext          = NULL,
    .flags          = 0,  // defaults to "wait_all"
    .semaphoreCount = 1,
    .pSemaphores    = deps->immediate.semaphores + immediate,
    .pValues        = deps->immediate.values + immediate,
  };

  //
  // Wait for semaphore to complete...
  //
  if (vkWaitSemaphores(vk->d, &swi, timeout) != VK_SUCCESS)
    {
      return false;
    }

  //
  // Execute the post-wait pfn *after* releasing the semaphore.
  //
  // clang-format off
  deps->immediate.submissions -= 1;
  deps->immediate.tail        += 1;
  // clang-format on

  if (deps->immediate.tail == deps->immediate.size)
    {
      deps->immediate.tail = 0;
    }

  spinel_deps_action_invoke(deps->immediate.completions + immediate);

  return true;
}

//
// Drain one immediate submission at a time
//
// FIXME(allanmac): Refactor to support VK_ERROR_DEVICE_LOST
//
bool
spinel_deps_drain_1(struct spinel_deps * deps, struct spinel_device_vk const * vk, uint64_t timeout)
{
  //
  // Anything to do?
  //
  if (deps->immediate.submissions == 0)
    {
      return false;
    }

  return spinel_deps_drain_tail(deps, vk, timeout);
}

//
// Drain all submissions
//
// FIXME(allanmac): Refactor to support VK_ERROR_DEVICE_LOST
//
void
spinel_deps_drain_all(struct spinel_deps * deps, struct spinel_device_vk const * vk)
{
  while (deps->immediate.submissions > 0)
    {
      spinel_deps_drain_tail(deps, vk, UINT64_MAX);  // ignore return value
    }
}

//
// Acquire a "delayed" semaphore
//
spinel_deps_delayed_semaphore_t
spinel_deps_delayed_acquire(struct spinel_deps *                            deps,
                            struct spinel_device_vk const *                 vk,
                            struct spinel_deps_acquire_delayed_info const * info)
{
  //
  // Wrap to zero?
  //
  uint32_t const delayed = deps->delayed.next++;

  if (deps->delayed.next >= deps->delayed.size)
    {
      deps->delayed.next = 0;
    }

  //
  // Invoke the submission action.
  //
  // This implicitly:
  //
  //   1. Invokes and clears the action.
  //   2. Submits along with a paired immediate semaphore.
  //   3. Increments the delayed semaphore's timeline signal value.
  //
  spinel_deps_action_invoke(deps->delayed.submissions + delayed);

  //
  // Save the new submission action
  //
  deps->delayed.submissions[delayed] = info->submission;

  //
  // Return the delayed semaphore
  //
  return (spinel_deps_delayed_semaphore_t)delayed;
}

//
// 1. If head is the first entry of a pool and there are active submissions in
//    the same pool then drain the tail entries until the pool has no active
//    submissions.
//
// 2. Probe remaining submissions until there is a timeout.
//
// 3. Return the head entry.
//
static spinel_deps_immediate_semaphore_t
spinel_deps_immediate_acquire(struct spinel_deps * deps, struct spinel_device_vk const * vk)
{
  while (true)
    {
      //
      // 1. If head is the first entry of a pool and there are active
      //    submissions in the same pool then drain the tail entries until the
      //    pool has no active submissions.
      //
      div_t const pool_quot_rem = div(deps->immediate.head, deps->immediate.pool.size);

      if (pool_quot_rem.rem == 0)
        {
          uint32_t const available = deps->immediate.size - deps->immediate.submissions;
          bool const     is_active = (available < deps->immediate.pool.size);

          if (is_active)
            {
              //
              // This command pool is active so drain the oldest and continue.
              //
              spinel_deps_drain_1(deps, vk, UINT64_MAX);

              continue;
            }
          else
            {
              //
              // This command pool isn't active so reset and proceed.
              //
              vk(ResetCommandPool(vk->d, deps->immediate.pool.extent[pool_quot_rem.quot], 0));
            }
        }

      //
      // 2. Probe remaining submissions until there is a timeout.
      //
      if (spinel_deps_drain_1(deps, vk, 0UL))
        {
          continue;
        }

      //
      // 3. Return the head entry.
      //
      deps->immediate.submissions += 1;

      uint32_t const immediate = deps->immediate.head++;

      if (deps->immediate.head >= deps->immediate.size)
        {
          deps->immediate.head = 0;
        }

      //
      // 4. Increment to future signal value
      //
      deps->immediate.values[immediate] += 1;

      return (spinel_deps_immediate_semaphore_t)immediate;
    }
}

//
// Note that this is the only place delayed semaphores are incremented.
//
static void
spinel_deps_signalset_append_delayed(struct spinel_deps const *                       deps,
                                     struct spinel_deps_signalset *                   signalset,
                                     struct spinel_deps_immediate_submit_info const * info)

{
  for (uint32_t ii = 0; ii < info->signal.delayed.count; ii++)
    {
      spinel_deps_delayed_semaphore_t const delayed = info->signal.delayed.semaphores[ii];

      signalset->semaphores[ii] = deps->delayed.semaphores[delayed];
      signalset->values[ii]     = ++deps->delayed.values[delayed];
    }

  signalset->count = info->signal.delayed.count;
}

//
//
//
static void
spinel_deps_signalset_append_immediate(struct spinel_deps const *                       deps,
                                       struct spinel_deps_signalset *                   signalset,
                                       struct spinel_deps_immediate_submit_info const * info,
                                       spinel_deps_immediate_semaphore_t                immediate)
{
  signalset->semaphores[signalset->count] = deps->immediate.semaphores[immediate];
  signalset->values[signalset->count]     = deps->immediate.values[immediate];

  signalset->count += 1;

  deps->immediate.completions[immediate] = info->completion;
}

//
//
//
static void
spinel_deps_signalset_append_transfer(struct spinel_deps const *                       deps,
                                      struct spinel_deps_signalset *                   signalset,
                                      struct spinel_deps_immediate_submit_info const * info)
{
  for (uint32_t ii = 0; ii < info->signal.transfer.count; ii++)
    {
      signalset->semaphores[signalset->count] = info->signal.transfer.semaphores[ii];
      signalset->values[signalset->count]     = info->signal.transfer.values[ii];

      signalset->count += 1;
    }
}

//
//
//
static void
spinel_deps_signalset_append_import(struct spinel_deps const *                       deps,
                                    struct spinel_deps_signalset *                   signalset,
                                    struct spinel_deps_immediate_submit_info const * info)
{
  for (uint32_t ii = 0; ii < info->signal.import.count; ii++)
    {
      signalset->semaphores[signalset->count] = info->signal.import.semaphores[ii];
      signalset->values[signalset->count]     = info->signal.import.values[ii];

      signalset->count += 1;
    }
}

//
// Acquire an "immediate" semaphore
//
// Immediate semaphores (info.immediates) have already been submitted.
//
// But delayed semaphores associated with handles (info.handles) may not have
// been submitted.
//
spinel_deps_immediate_semaphore_t
spinel_deps_immediate_submit(struct spinel_deps *                             deps,
                             struct spinel_device_vk *                        vk,
                             struct spinel_deps_immediate_submit_info const * info)
{
  assert(info->wait.immediate.count <= SPN_DEPS_IMMEDIATE_SUBMIT_SIZE_WAIT_IMMEDIATE);
  assert(info->signal.delayed.count <= SPN_DEPS_IMMEDIATE_SUBMIT_SIZE_SIGNAL_DELAYED);

  //
  // Gather immediate semaphores as well as delayed semaphores associated with a
  // ring span of handles.  Ensure all are submitted before continuing.
  //
  struct spinel_deps_waitset waitset;

  spinel_deps_waitset_init(deps, &waitset, info);

  //
  // Append transfer and import wait timelines
  //
  spinel_deps_waitset_append_transfer(deps, &waitset, info);
  spinel_deps_waitset_append_import(deps, &waitset, info);

  //
  // Flush any delayed semaphores that must be signalled and save their updated
  // values.
  //
  struct spinel_deps_signalset signalset;

  spinel_deps_signalset_append_delayed(deps, &signalset, info);

  //
  // Acquire immediate semaphore
  //
  spinel_deps_immediate_semaphore_t const immediate = spinel_deps_immediate_acquire(deps, vk);

  //
  // Append signalling acquired immediate semaphore, its new value, and
  // completion action.
  //
  spinel_deps_signalset_append_immediate(deps, &signalset, info, immediate);

  //
  // Append transfer and import signal timelines
  //
  spinel_deps_signalset_append_transfer(deps, &signalset, info);
  spinel_deps_signalset_append_import(deps, &signalset, info);

  //
  // Record commands
  //
  VkCommandBuffer cb = deps->immediate.cbs[immediate];

  VkCommandBufferBeginInfo const cbbi = {

    .sType            = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
    .pNext            = NULL,
    .flags            = 0,
    .pInheritanceInfo = NULL
  };

  vk(BeginCommandBuffer(cb, &cbbi));

  if (info->record.pfn != NULL)
    {
      deps->immediate.stages[immediate] = info->record.pfn(cb,                   //
                                                           info->record.data0,   //
                                                           info->record.data1);  //
    }
  else
    {
      deps->immediate.stages[immediate] = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
    }

  vk(EndCommandBuffer(cb));

  //
  // Submit the command buffer with its associated wait and signal timelines.
  //
  VkTimelineSemaphoreSubmitInfo const tssi = {

    .sType                     = VK_STRUCTURE_TYPE_TIMELINE_SEMAPHORE_SUBMIT_INFO,
    .pNext                     = NULL,
    .waitSemaphoreValueCount   = waitset.count,
    .pWaitSemaphoreValues      = waitset.values,
    .signalSemaphoreValueCount = signalset.count,
    .pSignalSemaphoreValues    = signalset.values
  };

  VkSubmitInfo const submit_info = {

    .sType                = VK_STRUCTURE_TYPE_SUBMIT_INFO,
    .pNext                = &tssi,
    .waitSemaphoreCount   = waitset.count,
    .pWaitSemaphores      = waitset.semaphores,
    .pWaitDstStageMask    = waitset.stages,
    .commandBufferCount   = 1,
    .pCommandBuffers      = &cb,
    .signalSemaphoreCount = signalset.count,
    .pSignalSemaphores    = signalset.semaphores
  };

  VkQueue q = spinel_queue_pool_get_next(&vk->q.compute);

  vk(QueueSubmit(q, 1, &submit_info, VK_NULL_HANDLE));

  return immediate;
}

//
//
//
VkPipelineStageFlags
spinel_deps_immediate_get_stage(struct spinel_deps *              deps,
                                spinel_deps_immediate_semaphore_t immediate)
{
  return deps->immediate.stages[immediate];
}

//
//
//
