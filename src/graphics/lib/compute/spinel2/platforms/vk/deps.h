// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_GRAPHICS_LIB_COMPUTE_SPINEL2_PLATFORMS_VK_DEPS_H_
#define SRC_GRAPHICS_LIB_COMPUTE_SPINEL2_PLATFORMS_VK_DEPS_H_

//
//
//

#include <stdbool.h>
#include <vulkan/vulkan_core.h>

#include "common/macros.h"
#include "queue_pool.h"
#include "spinel/platforms/vk/spinel_vk_types.h"
#include "spinel/spinel_types.h"

//
// Forwards
//
struct spinel_device_vk;

//
// Command buffer submissions and their dependencies are coordinated entirely
// with timeline semaphores.
//
// Theory of operation:
//
//   A client acquires a timeline semaphore and registers zero or more timeline
//   semaphores to wait upon.
//
//   The "step" of each timeline is implicitly 1.
//
//   There are two types of submissions in the Spinel pipeline:
//
//     1. Immediate Submission
//
//        A timeline semaphore and a command buffer are acquired for immediate
//        submission.
//
//        An acquisition will block until the timeline semaphore and command
//        buffer pair are available.
//
//        The submission always has a post-execution completion function.
//
//        The submission can wait upon:
//
//          - A few "Type 1" submissions.
//          - Potentially many "Type 2" submissions.
//
//     2. Delayed Submission
//
//        A timeline semaphore is acquired for future submission.
//
//        The primary use case is constructing a path or raster handle and
//        associating it with an unsubmitted timeline semaphore.
//
//        The submission always has a submission function.
//
//        The submission action will acquire an immediate semaphore and command
//        buffer using (1).  The command buffer will wait on zero or more
//        timelines and signal *BOTH* the submission semaphore (2) and the just
//        acquired semaphore from (1).
//
//   The path and raster builders depend on (2).
//
//   The remaining stages and dispatch submission functions are served by (1).
//
// Implementation:
//
//   - All command buffers are initialized with the ONE_TIME_SUBMIT_BIT flag.
//   - Only command pools are reset and not command buffers.
//   - The deps pool must support at least one immediate and one delayed
//     submission.
//
// Invariants:
//
//   - Various usage invariants are enforced in debug builds.
//

//
// Declare the max number of timeline semaphores.
//
// FIXME(allanmac): These are likely larger than necessary and can be lowered
// specific platforms (e.g. ARM SoCs).  Alternatively, just select a smaller
// number.
//
// clang-format off
#define SPN_DEPS_IMMEDIATE_SEMAPHORE_MAX  128
#define SPN_DEPS_DELAYED_SEMAPHORE_MAX    128
// clang-format on

//
// Derive types based on maximum number of semaphores.
//
// An invalid timeline semaphore is represented by first index after the last
// valid timeline semaphore.
//
#if (SPN_DEPS_IMMEDIATE_SEMAPHORE_MAX < 256)

typedef uint8_t spinel_deps_immediate_semaphore_t;
#define SPN_DEPS_IMMEDIATE_SEMAPHORE_INVALID UINT8_MAX

#elif (SPN_DEPS_IMMEDIATE_SEMAPHORE_MAX < 65536)

typedef uint16_t spinel_deps_immediate_semaphore_t;
#define SPN_DEPS_IMMEDIATE_SEMAPHORE_INVALID UINT16_MAX

#else
#error "Error: SPN_DEPS_IMMEDIATE_SEMAPHORE_MAX >= 65536"
#endif

#if (SPN_DEPS_DELAYED_SEMAPHORE_MAX < 256)

typedef uint8_t spinel_deps_delayed_semaphore_t;
#define SPN_DEPS_DELAYED_SEMAPHORE_INVALID UINT8_MAX

#elif (SPN_DEPS_DELAYED_SEMAPHORE_MAX < 65536)

#define SPN_DEPS_DELAYED_SEMAPHORE_INVALID UINT16_MAX
typedef uint16_t spinel_deps_delayed_semaphore_t;

#else
#error "Error: SPN_DEPS_DELAYED_SEMAPHORE_MAX >= 65536"
#endif

//
// Deps creation parameters
//
struct spinel_deps_create_info
{
  struct
  {
    struct
    {
      struct
      {
        uint32_t size;    // Size of immediate semaphore pool is
        uint32_t count;   // (pool.size * pool.count)
      } pool;             //
    } immediate;          //
                          //
    struct                //
    {                     //
      uint32_t size;      // Size of delayed semaphore pool
    } delayed;            //
                          //
  } semaphores;           //
                          //
  uint32_t handle_count;  // Matches number of handles in the handle pool
};

//
// Internal semaphore waits used for transfers
//
// Note that binary semaphores ignore associated values.
//
#define SPN_DEPS_TRANSFER_WAIT_SIZE 1

typedef struct spinel_deps_transfer_wait
{
  // clang-format off
  uint32_t             count;
  VkPipelineStageFlags stages    [SPN_DEPS_TRANSFER_WAIT_SIZE];
  VkSemaphore          semaphores[SPN_DEPS_TRANSFER_WAIT_SIZE];
  uint64_t             values    [SPN_DEPS_TRANSFER_WAIT_SIZE];
  // clang-format on
} spinel_deps_transfer_wait_t;

//
// Internal semaphore signals used for transfers
//
// Note that binary semaphores ignore associated values.
//
#define SPN_DEPS_TRANSFER_SIGNAL_SIZE 1

typedef struct spinel_deps_transfer_signal
{
  // clang-format off
  uint32_t    count;
  VkSemaphore semaphores[SPN_DEPS_TRANSFER_SIGNAL_SIZE];
  uint64_t    values    [SPN_DEPS_TRANSFER_SIGNAL_SIZE];
  // clang-format on
} spinel_deps_transfer_signal_t;

//
// There are both completion and submission actions but they have the same
// signature.
//
typedef void (*spinel_deps_pfn_t)(void * data0, void * data1);

struct spinel_deps_action
{
  spinel_deps_pfn_t pfn;
  void *            data0;
  void *            data1;
};

//
// Record to a command buffer and return the final pipeline stage.
//
typedef VkPipelineStageFlags (*spinel_deps_immediate_record_pfn_t)(VkCommandBuffer cb,
                                                                   void *          data0,
                                                                   void *          data1);

//
// Fixed size limits on the immediate submit info structure.
//
// FIXME(allanmac): Adjust the immediate count to its limit.  The composition
// might be the only object needing to wait on more than a few PLACE immediate
// submissions.
//
#define SPN_DEPS_IMMEDIATE_SUBMIT_SIZE_WAIT_IMMEDIATE 33
#define SPN_DEPS_IMMEDIATE_SUBMIT_SIZE_SIGNAL_DELAYED 1

//
// "Immediate submit" arguments
//
struct spinel_deps_immediate_submit_info
{
  struct
  {
    spinel_deps_immediate_record_pfn_t pfn;
    void *                             data0;
    void *                             data1;
  } record;  // Record Vulkan commands

  struct
  {
    struct
    {
      uint32_t                          count;
      spinel_deps_immediate_semaphore_t semaphores[SPN_DEPS_IMMEDIATE_SUBMIT_SIZE_WAIT_IMMEDIATE];
    } immediate;

    struct
    {
      struct
      {
        spinel_handle_t const * extent;
        uint32_t                size;
        uint32_t                head;
        uint32_t                span;
      } handles;
    } delayed;

    spinel_deps_transfer_wait_t       transfer;
    spinel_vk_semaphore_import_wait_t import;
  } wait;

  struct
  {
    struct
    {
      uint32_t                        count;
      spinel_deps_delayed_semaphore_t semaphores[SPN_DEPS_IMMEDIATE_SUBMIT_SIZE_SIGNAL_DELAYED];
    } delayed;

    spinel_deps_transfer_signal_t       transfer;
    spinel_vk_semaphore_import_signal_t import;
  } signal;

  struct spinel_deps_action completion;
};

//
// "Delayed acquire" arguments
//
struct spinel_deps_acquire_delayed_info
{
  struct spinel_deps_action submission;
};

//
// Create deps instance
//
struct spinel_deps *
spinel_deps_create(struct spinel_deps_create_info const * info, struct spinel_device_vk const * vk);

//
// Dispose of deps instance
//
void
spinel_deps_dispose(struct spinel_deps * deps, struct spinel_device_vk const * vk);

//
// Acquire a "delayed" semaphore
//
spinel_deps_delayed_semaphore_t
spinel_deps_delayed_acquire(struct spinel_deps *                            deps,
                            struct spinel_device_vk const *                 vk,
                            struct spinel_deps_acquire_delayed_info const * info);

//
// Attach a single delayed semaphore to a handle
//
void
spinel_deps_delayed_attach(struct spinel_deps *            deps,
                           spinel_handle_t                 handle,
                           spinel_deps_delayed_semaphore_t semaphore);

//
// Detach a delayed semaphore from an extent of handles
//
void
spinel_deps_delayed_detach(struct spinel_deps *    deps,
                           spinel_handle_t const * handles,
                           uint32_t                count);

//
// Detach a delayed semaphore from a ring of handles
//
void
spinel_deps_delayed_detach_ring(struct spinel_deps *    deps,
                                spinel_handle_t const * handles,
                                uint32_t                size,
                                uint32_t                head,
                                uint32_t                span);

//
// Flush a delayed semaphore
//
void
spinel_deps_delayed_flush(struct spinel_deps * deps, spinel_deps_delayed_semaphore_t delayed);

//
// An immediate submission will only ever wait on small number of prior
// immediate semaphores.  This is statically known.
//
void
spinel_deps_immediate_submit(struct spinel_deps *                             deps,
                             struct spinel_device_vk *                        vk,
                             struct spinel_deps_immediate_submit_info const * info,
                             spinel_deps_immediate_semaphore_t *              p_immediate);

//
// Get the final stage of the submission associated with `immediate`
//
VkPipelineStageFlags
spinel_deps_immediate_get_stage(struct spinel_deps *              deps,
                                spinel_deps_immediate_semaphore_t immediate);

//
// Blocks until:
//
//   * At least one completion action is executed
//   * Or a submission is completed and its action is executed.
//
// Returns true if either case is true.
//
bool
spinel_deps_drain_1(struct spinel_deps * deps, struct spinel_device_vk const * vk);

//
// Blocks until all submissions and actions are drained.
//
void
spinel_deps_drain_all(struct spinel_deps *            deps,  //
                      struct spinel_device_vk const * vk);

//
//
//

#endif  // SRC_GRAPHICS_LIB_COMPUTE_SPINEL2_PLATFORMS_VK_DEPS_H_
