// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "dispatch.h"

#include <stdio.h>
#include <string.h>

#include "common/macros.h"
#include "common/vk/assert.h"
#include "device.h"
#include "queue_pool.h"

//
// NOTE: dispatch.c is reentrant but single-threaded (for now)
//

//
// FOR DEBUG ONLY
//
// Track outstanding number of dispatches that are waiting on prior
// dispatches to complete.
//

#ifndef NDEBUG
#define SPN_DISPATCH_TRACK_WAITING
#endif

//
// NOTE:
//
// It's unlikely we'll want to support more than 254 outstanding
// dispatch ids unless we're running on an extremely large GPU.
//
// Note that 255 in-flight or waiting dispatches represents a very large
// amount of processing.
//
// Increasing this limit to either a total of 1024 dispatches or up to
// 1024 per stage would be extreme.
//
// There are two angles of attack here:
//
//   1. Implement a per stage id pool and hide a tag in the dispatch id.
//
//   2. Increase the size of the stage_id type to 16 bits.
//
// One implication of a larger spn_dispatch_stage_id_t is that we store
// one per handle in handle pool.  This is a strong reason to choose
// option (1).
//

// clang-format off
#define SPN_DISPATCH_ID_BITS (8 * sizeof(spn_dispatch_id_t))
#define SPN_DISPATCH_ID_COUNT BITS_TO_MASK_MACRO(SPN_DISPATCH_STAGE_ID_BITS)
// clang-format on

//
// The dispatch_stage_id_t may expand to a larger type and include a tag.
//

typedef uint8_t spn_dispatch_stage_id_t;

// clang-format off
#define SPN_DISPATCH_STAGE_ID_BITS     (8 * sizeof(spn_dispatch_stage_id_t))
#define SPN_DISPATCH_STAGE_ID_INVALID  BITS_TO_MASK_MACRO(SPN_DISPATCH_STAGE_ID_BITS)
#define SPN_DISPATCH_STAGE_ID_COUNT    BITS_TO_MASK_MACRO(SPN_DISPATCH_STAGE_ID_BITS)
// clang-format on

//
// Type determined by max number of dispatches that can be waited upon.
//
// This may vary by stage in a future implementation.
//

typedef spn_dispatch_id_t spn_dispatch_stage_wait_count_t;  // same size for now

//
// The completion payload size limit is currently 48 bytes.
//
// Lower this if the submission callback payloads shrink further.
//

// clang-format off
#define SPN_DISPATCH_COMPLETION_PAYLOAD_QWORDS  6
#define SPN_DISPATCH_COMPLETION_PAYLOAD_SIZE    MEMBER_SIZE_MACRO(struct spn_dispatch_completion, payload)
// clang-format on

//
// NOTE: We're forever limiting the signalling bitmap to a massive 1024
// dispatch ids per stage.
//
// If the stage id is  8 bits, spn_dispatch_signal is  9 dwords (36 bytes).
// If the stage id is 10 bits, spn_dispatch_signal is 33 dwords (132 bytes).
//

// clang-format off
#define SPN_DISPATCH_SIGNAL_BITMAP_DWORDS MIN_MACRO(uint32_t, 32, ((1 << SPN_DISPATCH_STAGE_ID_BITS) / 32))
#define SPN_DISPATCH_SIGNAL_BITMAP_SIZE   MEMBER_SIZE_MACRO(struct spn_dispatch_signal, bitmap)
// clang-format on

struct spn_dispatch_signal
{
  uint32_t index;
  uint32_t bitmap[SPN_DISPATCH_SIGNAL_BITMAP_DWORDS];
};

//
// The arg is an spn_[path|raster]_builder_impl pointer.
//

struct spn_dispatch_flush
{
  void * arg;
};

//
// When a dispatch completes, it may invoke a completion routine to
// reclaim resources and/or dispatch more work.
//

struct spn_dispatch_completion
{
  spn_dispatch_completion_pfn_t pfn;
  uint64_t                      payload[SPN_DISPATCH_COMPLETION_PAYLOAD_QWORDS];
};

//
//
//

struct spn_dispatch_submitter
{
  spn_dispatch_submitter_pfn_t pfn;
  void *                       data;
};

//
//
//

struct spn_dispatch
{
  VkCommandPool cp;

  VkCommandBuffer                 cbs[SPN_DISPATCH_ID_COUNT];
  VkFence                         fences[SPN_DISPATCH_ID_COUNT];
  struct spn_dispatch_signal      signals[SPN_DISPATCH_ID_COUNT];
  struct spn_dispatch_flush       flushes[SPN_DISPATCH_ID_COUNT];
  struct spn_dispatch_submitter   submitters[SPN_DISPATCH_ID_COUNT];
  struct spn_dispatch_completion  completions[SPN_DISPATCH_ID_COUNT];
  spn_dispatch_stage_wait_count_t wait_counts[SPN_DISPATCH_ID_COUNT];

  struct spn_dispatch_id_count
  {
    uint32_t available;
    uint32_t executing;
    uint32_t complete;

#ifdef SPN_DISPATCH_TRACK_WAITING
    uint32_t waiting;  // NOTE(allanmac): debug only
#endif
  } counts;

  struct
  {
    spn_dispatch_stage_id_t available[SPN_DISPATCH_STAGE_ID_COUNT];
    spn_dispatch_id_t       executing[SPN_DISPATCH_ID_COUNT];
    spn_dispatch_id_t       complete[SPN_DISPATCH_ID_COUNT];
  } indices;

  // a large array that maps handle ids to dispatch stage ids
  spn_dispatch_stage_id_t * handle_stage_ids;
};

//
//
//

void
spn_device_dispatch_create(struct spn_device * const device)
{
  //
  // allocate
  //
  struct spn_dispatch * const dispatch = spn_allocator_host_perm_alloc(&device->allocator.host.perm,
                                                                       SPN_MEM_FLAGS_READ_WRITE,
                                                                       sizeof(*dispatch));
  //
  // hang it off the device
  //
  device->dispatch = dispatch;

  //
  // create command pool
  //
  VkCommandPoolCreateInfo const cpci = {
    //
    // FIXME(allanmac): I don't think we are actually TRANSIENT so I'm
    // not indicating so with a flag.  The command buffers can be held
    // for a while before being submitted.
    //
    .sType            = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
    .pNext            = NULL,
    .flags            = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,
    .queueFamilyIndex = device->environment.qfi
  };

  vk(CreateCommandPool(device->environment.d, &cpci, device->environment.ac, &dispatch->cp));

  //
  // create command buffers
  //
  VkCommandBufferAllocateInfo const cbai = {

    .sType              = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
    .pNext              = NULL,
    .commandPool        = dispatch->cp,
    .level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
    .commandBufferCount = SPN_DISPATCH_ID_COUNT
  };

  vk(AllocateCommandBuffers(device->environment.d, &cbai, dispatch->cbs));

  //
  // create fences
  //
  VkFenceCreateInfo const fci = {

    .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
    .pNext = NULL,
    .flags = 0
  };

  for (uint32_t ii = 0; ii < SPN_DISPATCH_ID_COUNT; ii++)
    {
      vk(CreateFence(device->environment.d, &fci, device->environment.ac, dispatch->fences + ii));
    }

  //
  // allocate and initialize handle stage ids
  //
  uint32_t const handle_count          = spn_device_handle_pool_get_allocated_handle_count(device);
  size_t const   handle_stage_ids_size = sizeof(*dispatch->handle_stage_ids) * handle_count;

  dispatch->handle_stage_ids = spn_allocator_host_perm_alloc(&device->allocator.host.perm,
                                                             SPN_MEM_FLAGS_READ_WRITE,
                                                             handle_stage_ids_size);

  memset(dispatch->handle_stage_ids, SPN_DISPATCH_STAGE_ID_INVALID, handle_stage_ids_size);

  //
  // initialize counts and indices
  //
  dispatch->counts.available = SPN_DISPATCH_STAGE_ID_COUNT;
  dispatch->counts.executing = 0;
  dispatch->counts.complete  = 0;

#ifdef SPN_DISPATCH_TRACK_WAITING
  dispatch->counts.waiting = 0;
#endif

  for (uint32_t ii = 0; ii < SPN_DISPATCH_STAGE_ID_COUNT; ii++)
    {
      dispatch->indices.available[ii] = ii;
    }
}

//
//
//

void
spn_device_dispatch_dispose(struct spn_device * const device)
{
  struct spn_dispatch * const dispatch = device->dispatch;

  //
  // FIXME -- interrupt and free VkFences
  //

  // destroy fences
  for (uint32_t ii = 0; ii < SPN_DISPATCH_ID_COUNT; ii++)
    {
      vkDestroyFence(device->environment.d, dispatch->fences[ii], device->environment.ac);
    }

  // free command buffers
  vkFreeCommandBuffers(device->environment.d, dispatch->cp, SPN_DISPATCH_ID_COUNT, dispatch->cbs);

  // destroy command pool
  vkDestroyCommandPool(device->environment.d, dispatch->cp, device->environment.ac);

  // free handle stage ids
  spn_allocator_host_perm_free(&device->allocator.host.perm, dispatch->handle_stage_ids);

  // free dispatch
  spn_allocator_host_perm_free(&device->allocator.host.perm, dispatch);
}

//
//
//

static void
spn_device_dispatch_submitter_default(VkQueue               queue,
                                      VkFence               fence,
                                      VkCommandBuffer const cb,
                                      void *                data)
{
  struct VkSubmitInfo const si = { .sType              = VK_STRUCTURE_TYPE_SUBMIT_INFO,
                                   .commandBufferCount = 1,
                                   .pCommandBuffers    = &cb };

  vk(QueueSubmit(queue, 1, &si, fence));
}

//
//
//

static void
spn_device_dispatch_signal_waiters_dword(struct spn_device * const   device,
                                         struct spn_dispatch * const dispatch,
                                         uint32_t const              bitmap_base,
                                         uint32_t                    bitmap_dword)
{
  do
    {
      uint32_t const lsb_plus_1 = __builtin_ffs(bitmap_dword);
      uint32_t const lsb        = lsb_plus_1 - 1;
      uint32_t const mask       = 1u << lsb;

      // mask off lsb
      bitmap_dword &= ~mask;

      // which dispatch id?
      spn_dispatch_id_t const id = bitmap_base + lsb;

      // submit command buffer?
      spn_dispatch_stage_wait_count_t const wait_count = --dispatch->wait_counts[id];

      if (wait_count == 0)
        {
#ifdef SPN_DISPATCH_TRACK_WAITING
          // decrement the waiting count
          dispatch->counts.waiting -= 1;
#endif
          // push to executing -- coerce to possibly narrower integer type
          dispatch->indices.executing[dispatch->counts.executing++] = id;

          // submit!
          dispatch->submitters[id].pfn(spn_device_queue_next(device),
                                       dispatch->fences[id],
                                       dispatch->cbs[id],
                                       dispatch->submitters[id].data);
        }
    }
  while (bitmap_dword != 0);
}

static void
spn_device_dispatch_signal_waiters(struct spn_device * const                device,
                                   struct spn_dispatch * const              dispatch,
                                   struct spn_dispatch_signal const * const signal)
{
  //
  // for all dispatch ids in the bitmap
  //   - decrement the count of the bit dispatch
  //   - if zero then add to the executing list and submit
  //
  uint32_t index = signal->index;

  do
    {
      // which bit is lit?
      uint32_t const lsb_plus_1 = __builtin_ffs(index);
      uint32_t const lsb        = lsb_plus_1 - 1;
      uint32_t const mask       = 1u << lsb;

      // mask off lsb
      index &= ~mask;

      // process one dword of the bitmap
      spn_device_dispatch_signal_waiters_dword(device, dispatch, lsb * 32, signal->bitmap[lsb]);
    }
  while (index != 0);
}

//
//
//

static void
spn_device_dispatch_process_complete(struct spn_device * const   device,
                                     struct spn_dispatch * const dispatch)
{
  //
  // it's possible that excution of a pfn will update this count
  //
  while (dispatch->counts.complete > 0)
    {
      spn_dispatch_id_t const id = dispatch->indices.complete[--dispatch->counts.complete];

      // is there a pfn?
      struct spn_dispatch_completion * const completion = dispatch->completions + id;
      bool const                             is_pfn     = (completion->pfn != NULL);

      // are there dispatches waiting for a signal?
      struct spn_dispatch_signal const * const dispatch_signal = dispatch->signals + id;
      bool const                               is_signalling   = (dispatch_signal->index != 0);

      // NOTE: we make the dispatch available *before* invoking the callback
      dispatch->indices.available[dispatch->counts.available++] = id;

      // invoke completion
      if (is_pfn && is_signalling)
        {
          // save the pfn payload -- ~48 bytes
          uint64_t payload[SPN_DISPATCH_COMPLETION_PAYLOAD_QWORDS];

          memcpy(payload, completion->payload, SPN_DISPATCH_COMPLETION_PAYLOAD_SIZE);

          // save the signals -- ~36 bytes
          struct spn_dispatch_signal const dispatch_signal_copy = *dispatch_signal;

          // invoke pfn
          completion->pfn(payload);

          // signal waiters
          spn_device_dispatch_signal_waiters(device, dispatch, &dispatch_signal_copy);
        }
      else if (is_pfn)
        {
          // save the pfn payload -- ~48 bytes
          uint64_t payload[SPN_DISPATCH_COMPLETION_PAYLOAD_QWORDS];

          memcpy(payload, completion->payload, SPN_DISPATCH_COMPLETION_PAYLOAD_SIZE);

          // invoke pfn
          completion->pfn(payload);
        }
      else if (is_signalling)
        {
          // save the signals -- ~36 bytes
          struct spn_dispatch_signal const dispatch_signal_copy = *dispatch_signal;

          // signal waiters
          spn_device_dispatch_signal_waiters(device, dispatch, &dispatch_signal_copy);
        }
    }
}

//
//
//

static spn_result_t
spn_device_dispatch_process_executing(struct spn_device * const   device,
                                      struct spn_dispatch * const dispatch,
                                      uint32_t const              imports_count,
                                      VkFence * const             imports,
                                      bool const                  wait_all,
                                      uint64_t const              timeout_ns)
{
  uint32_t const count_executing = dispatch->counts.executing;
  uint32_t const fences_count    = count_executing + imports_count;

  if (fences_count == 0)
    {
      return SPN_SUCCESS;
    }

  //
  // VkWaitForFences() requires a linear array of VkFences
  //
  // The executing fences are followed by the imports.
  //
  // For now, this uses a VLA.
  //
  VkFence fences[fences_count];

  for (uint32_t ii = 0; ii < count_executing; ii++)
    {
      fences[ii] = dispatch->fences[dispatch->indices.executing[ii]];
    }

  //
  // skip if no imports
  //
  if (imports_count > 0)
    {
      memcpy(fences + count_executing, imports, sizeof(*imports) * imports_count);
    }

  //
  // wait for signalled or timeout
  //
  switch (vkWaitForFences(device->environment.d, fences_count, fences, wait_all, timeout_ns))
    {
      case VK_SUCCESS:
        break;

      case VK_TIMEOUT:
        return SPN_TIMEOUT;

      default:
        spn_device_lost(device);
        return SPN_ERROR_CONTEXT_LOST;
    }

  //
  // collect signalled dispatches...
  //
  uint32_t still_executing = 0;

  for (uint32_t ii = 0; ii < count_executing; ii++)
    {
      spn_dispatch_id_t const id = dispatch->indices.executing[ii];

      switch (vkGetFenceStatus(device->environment.d, fences[ii]))
        {
          case VK_SUCCESS:
            dispatch->indices.complete[dispatch->counts.complete++] = id;
            break;

          case VK_NOT_READY:
            dispatch->indices.executing[still_executing++] = id;
            break;

          default:
            spn_device_lost(device);
            return SPN_ERROR_CONTEXT_LOST;
        }
    }

  //
  // save count of dispatches that weren't signalled
  //
  dispatch->counts.executing = still_executing;

  return SPN_SUCCESS;
}

//
//
//

spn_result_t
spn_device_wait_for_fences(struct spn_device * const device,
                           uint32_t const            imports_count,
                           VkFence * const           imports,
                           bool const                wait_all,
                           uint64_t const            timeout_ns)
{
  struct spn_dispatch * const dispatch = device->dispatch;

  spn_result_t const result = spn_device_dispatch_process_executing(device,
                                                                    dispatch,
                                                                    imports_count,
                                                                    imports,
                                                                    wait_all,
                                                                    timeout_ns);

  // return timeout or worse...
  if (result != SPN_SUCCESS)
    return result;

  // otherwise, process completed dispatches...
  spn_device_dispatch_process_complete(device, dispatch);

  return SPN_SUCCESS;
}

//
//
//

spn_result_t
spn_device_wait_all(struct spn_device * const device, bool const wait_all)
{
  return spn_device_wait_for_fences(device, 0, NULL, wait_all, spn_device_get_timeout_ns(device));
}

spn_result_t
spn_device_wait(struct spn_device * const device)
{
  return spn_device_wait_all(device, false);
}

spn_result_t
spn_device_wait_verbose(struct spn_device * const device,
                        char const * const        file_line,
                        char const * const        func_name)
{
#ifndef SPN_DEVICE_WAIT_DEBUG_DISABLED
  fprintf(stderr, "%s %s() calls %s()\n", file_line, func_name, __func__);
#endif

  return spn_device_wait_all(device, false);
}

//
//
//

spn_result_t
spn_device_dispatch_acquire(struct spn_device * const  device,
                            spn_dispatch_stage_e const stage,
                            spn_dispatch_id_t * const  id)
{
  struct spn_dispatch * const dispatch = device->dispatch;

  // any available?
  while (dispatch->counts.available == 0)
    {
      spn_result_t const result = spn_device_wait(device);

      if (result)
        return result;
    }

  // pop
  *id = dispatch->indices.available[--dispatch->counts.available];

  // reset the fence
  vk(ResetFences(device->environment.d, 1, dispatch->fences + *id));

  // zero the signals
  struct spn_dispatch_signal * signal = dispatch->signals + *id;

  memset(signal, 0, sizeof(*signal));

  // zero the wait count
  dispatch->wait_counts[*id] = 0;

  // NULL the completion pfn
  dispatch->completions[*id].pfn = NULL;

  // set up default pfn/data
  dispatch->submitters[*id] =
    (struct spn_dispatch_submitter){ .pfn = spn_device_dispatch_submitter_default, .data = NULL };

  return SPN_SUCCESS;
}

//
//
//

VkCommandBuffer
spn_device_dispatch_get_cb(struct spn_device * const device, spn_dispatch_id_t const id)
{
  struct spn_dispatch * const dispatch = device->dispatch;

  VkCommandBuffer cb = dispatch->cbs[id];

  VkCommandBufferBeginInfo const cbbi = {

    .sType            = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
    .pNext            = NULL,
    .flags            = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
    .pInheritanceInfo = NULL
  };

  vk(BeginCommandBuffer(cb, &cbbi));

  return cb;
}

void
spn_device_dispatch_set_submitter(struct spn_device * const          device,
                                  spn_dispatch_id_t const            id,
                                  spn_dispatch_submitter_pfn_t const submitter_pfn,
                                  void *                             submitter_data)
{
  struct spn_dispatch * const dispatch = device->dispatch;

  // save pfn and data
  struct spn_dispatch_submitter * const submitter = dispatch->submitters + id;

  submitter->pfn  = submitter_pfn;
  submitter->data = submitter_data;
}

void *
spn_device_dispatch_set_completion(struct spn_device * const           device,
                                   spn_dispatch_id_t const             id,
                                   spn_dispatch_completion_pfn_t const completion_pfn,
                                   size_t const                        completion_payload_size)
{
  assert(completion_payload_size <= SPN_DISPATCH_COMPLETION_PAYLOAD_SIZE);

  struct spn_dispatch * const dispatch = device->dispatch;

  // save pfn and return payload
  struct spn_dispatch_completion * const completion = dispatch->completions + id;

  completion->pfn = completion_pfn;

  return completion->payload;
}

//
//
//

void
spn_device_dispatch_set_flush_arg(struct spn_device * const device,
                                  spn_dispatch_id_t const   id,
                                  void *                    arg)
{
  struct spn_dispatch * const dispatch = device->dispatch;

  // save pfn and return payload
  struct spn_dispatch_flush * const flush = dispatch->flushes + id;

  flush->arg = arg;
}

void
spn_device_dispatch_reset_flush_arg(struct spn_device * const device, spn_dispatch_id_t const id)
{
  struct spn_dispatch * const dispatch = device->dispatch;

  // save pfn and return payload
  struct spn_dispatch_flush * const flush = dispatch->flushes + id;

  flush->arg = NULL;
}

//
//
//

void
spn_device_dispatch_submit(struct spn_device * const device, spn_dispatch_id_t const id)
{
  struct spn_dispatch * const dispatch = device->dispatch;

  //
  // end the command buffer
  //
  vk(EndCommandBuffer(dispatch->cbs[id]));

  //
  // shortcut: launch immediately if there are no dependencies
  //
  spn_dispatch_stage_wait_count_t const wait_count = dispatch->wait_counts[id];

  if (wait_count == 0)
    {
      // push to executing
      dispatch->indices.executing[dispatch->counts.executing++] = id;

      // submit!
      dispatch->submitters[id].pfn(spn_device_queue_next(device),
                                   dispatch->fences[id],
                                   dispatch->cbs[id],
                                   dispatch->submitters[id].data);
    }
#ifdef SPN_DISPATCH_TRACK_WAITING
  else
    {
      dispatch->counts.waiting += 1;
    }
#endif
}

//
//
//

void
spn_device_dispatch_happens_after(struct spn_device * const device,
                                  spn_dispatch_id_t const   id_after,
                                  spn_dispatch_id_t const   id_before)
{
  struct spn_dispatch * const dispatch = device->dispatch;

  uint32_t const bitmap_after_dword_idx  = id_after / 32;
  uint32_t const bitmap_after_dword_bit  = id_after & 31;
  uint32_t const bitmap_after_dword_mask = 1u << bitmap_after_dword_bit;

  struct spn_dispatch_signal * const signal = dispatch->signals + id_before;

  uint32_t * const bitmap_after_dword = signal->bitmap + bitmap_after_dword_idx;

  uint32_t const curr = *bitmap_after_dword;
  uint32_t const next = (curr | bitmap_after_dword_mask);

  if (next != curr)
    {
      // update the index dword
      uint32_t const bitmap_after_dword_mask = 1u << bitmap_after_dword_idx;

      signal->index |= bitmap_after_dword_mask;

      // update the bitmap dword
      *bitmap_after_dword = next;

      dispatch->wait_counts[id_after] += 1;
    }
}

//
//
//

static void
spn_dispatch_flush_dword(struct spn_dispatch * const    dispatch,
                         spn_dispatch_flush_pfn_t const flush_pfn,
                         uint32_t const                 bitmap_base,
                         uint32_t                       bitmap_dword)
{
  do
    {
      uint32_t const lsb_plus_1 = __builtin_ffs(bitmap_dword);
      uint32_t const lsb        = lsb_plus_1 - 1;
      uint32_t const mask       = 1u << lsb;

      // mask off lsb
      bitmap_dword &= ~mask;

      // which dispatch?
      uint32_t const idx = bitmap_base + lsb;

      // invoke flush once
      struct spn_dispatch_flush * const flush = dispatch->flushes + idx;

      if (flush->arg != NULL)
        {
          flush_pfn(flush->arg);

          flush->arg = NULL;
        }
    }
  while (bitmap_dword != 0);
}

//
//
//

static void
spn_dispatch_flush(struct spn_dispatch * const              dispatch,
                   spn_dispatch_flush_pfn_t const           flush_pfn,
                   struct spn_dispatch_signal const * const signal)
{
  // anything to do?
  if (signal->index == 0)
    return;

  //
  // for all dispatch ids in the bitmap
  //   - if the flush pfn is not NULL then invoke
  //
  uint32_t index = signal->index;

  do
    {
      // which bit is lit?
      uint32_t const lsb_plus_1 = __builtin_ffs(index);
      uint32_t const lsb        = lsb_plus_1 - 1;
      uint32_t const mask       = 1u << lsb;

      // mask off lsb
      index &= ~mask;

      // process one dword of the bitmap
      spn_dispatch_flush_dword(dispatch, flush_pfn, lsb * 32, signal->bitmap[lsb]);
    }
  while (index != 0);
}

//
//
//

static void
spn_dispatch_happens_after_dword(struct spn_dispatch * const dispatch,
                                 uint32_t const              bitmap_after_index_mask,
                                 uint32_t const              bitmap_after_dword_idx,
                                 uint32_t const              bitmap_after_dword_mask,
                                 uint32_t const              bitmap_before_base,
                                 uint32_t                    bitmap_before_dword)
{
  do
    {
      // which bit is lit?
      uint32_t const lsb_plus_1 = __builtin_ffs(bitmap_before_dword);
      uint32_t const lsb        = lsb_plus_1 - 1;
      uint32_t const mask       = 1u << lsb;

      // mask off lsb
      bitmap_before_dword &= ~mask;

      // which dispatch?
      uint32_t const idx = bitmap_before_base + lsb;

      // get the signaller
      struct spn_dispatch_signal * const signal = dispatch->signals + idx;

      // update the signaller index
      signal->index |= bitmap_after_index_mask;

      // update the signaller bitmap
      signal->bitmap[bitmap_after_dword_idx] |= bitmap_after_dword_mask;
    }
  while (bitmap_before_dword != 0);
}

//
//
//

static void
spn_dispatch_accumulate_stage_ids(uint32_t * const                bitmap,
                                  spn_dispatch_stage_id_t * const stage_ids,
                                  spn_handle_t const * const      handles,
                                  uint32_t const                  count)
{
  for (uint32_t ii = 0; ii < count; ii++)
    {
      spn_handle_t const handle = handles[ii];

      spn_dispatch_stage_id_t const stage_id = stage_ids[handle];

      if (stage_id < SPN_DISPATCH_STAGE_ID_INVALID)
        {
          uint32_t const bitmap_dword_idx  = stage_id / 32;
          uint32_t const bitmap_dword_bit  = stage_id & 31;
          uint32_t const bitmap_dword_mask = 1u << bitmap_dword_bit;

          bitmap[bitmap_dword_idx] |= bitmap_dword_mask;
        }
    }
}

//
// NOTE(allanmac): We need to enforce that there is a maximum total
// number of path and raster builders in order to avoid deadlock.
//
// Unlike other Spinel dispatch clients, the path and raster builders
// acquire and hold a dispatch well before launch.
//
// Note that the span will never be zero.
//

void
spn_device_dispatch_happens_after_handles(struct spn_device * const      device,
                                          spn_dispatch_flush_pfn_t const flush_pfn,
                                          spn_dispatch_id_t const        id_after,
                                          spn_handle_t const * const     handles,
                                          uint32_t const                 size,
                                          uint32_t const                 span,
                                          uint32_t const                 head)
{
  //
  // accumulate all dependencies to bitmap
  //
  struct spn_dispatch * const dispatch = device->dispatch;

  struct spn_dispatch_signal signal_before = { 0 };

  uint32_t const count_lo = MIN_MACRO(uint32_t, head + span, size) - head;

  spn_dispatch_accumulate_stage_ids(signal_before.bitmap,
                                    dispatch->handle_stage_ids,
                                    handles + head,
                                    count_lo);

  if (span > count_lo)
    {
      uint32_t const count_hi = span - count_lo;

      spn_dispatch_accumulate_stage_ids(signal_before.bitmap,
                                        dispatch->handle_stage_ids,
                                        handles,
                                        count_hi);
    }

  //
  // update all dependencies with id_after
  //
  uint32_t const bitmap_after_dword_idx  = id_after / 32;
  uint32_t const bitmap_after_index_mask = 1u << bitmap_after_dword_idx;
  uint32_t const bitmap_after_dword_bit  = id_after & 31;
  uint32_t const bitmap_after_dword_mask = 1u << bitmap_after_dword_bit;

  uint32_t wait_count = 0;

  for (uint32_t ii = 0; ii < SPN_DISPATCH_SIGNAL_BITMAP_DWORDS; ii++)
    {
      uint32_t bitmap_before_dword = signal_before.bitmap[ii];

      if (bitmap_before_dword != 0)
        {
          // update index
          signal_before.index |= (1u << ii);

          // accumulate count
          wait_count += __builtin_popcount(bitmap_before_dword);

          // update signaller
          spn_dispatch_happens_after_dword(dispatch,
                                           bitmap_after_index_mask,
                                           bitmap_after_dword_idx,
                                           bitmap_after_dword_mask,
                                           ii * 32,
                                           bitmap_before_dword);
        }
    }

  //
  // update wait count
  //
  if (wait_count > 0)
    {
      dispatch->wait_counts[id_after] += wait_count;
    }

  //
  // flush all dependencies
  //
  spn_dispatch_flush(dispatch, flush_pfn, &signal_before);
}

//
//
//

void
spn_device_dispatch_register_handle(struct spn_device * const device,
                                    spn_dispatch_id_t const   id,
                                    spn_handle_t const        handle)
{
  struct spn_dispatch * const dispatch = device->dispatch;

  dispatch->handle_stage_ids[handle] = id;
}

//
//
//

static void
spn_dispatch_stage_ids_invalidate(spn_dispatch_stage_id_t * const stage_ids,
                                  spn_handle_t const * const      handles,
                                  uint32_t const                  count)
{
  for (uint32_t ii = 0; ii < count; ii++)
    {
      spn_handle_t const handle = handles[ii];

      stage_ids[handle] = SPN_DISPATCH_STAGE_ID_INVALID;
    }
}

//
// invalidate the ring span of handles
//

void
spn_device_dispatch_handles_complete(struct spn_device * const  device,
                                     spn_handle_t const * const handles,
                                     uint32_t const             size,
                                     uint32_t const             span,
                                     uint32_t const             head)
{
  struct spn_dispatch * const dispatch = device->dispatch;

  uint32_t const count_lo = MIN_MACRO(uint32_t, head + span, size) - head;

  spn_dispatch_stage_ids_invalidate(dispatch->handle_stage_ids, handles + head, count_lo);

  if (span > count_lo)
    {
      uint32_t const count_hi = span - count_lo;

      spn_dispatch_stage_ids_invalidate(dispatch->handle_stage_ids, handles, count_hi);
    }
}

//
//
//
