// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_GRAPHICS_LIB_COMPUTE_SPINEL_PLATFORMS_VK_DISPATCH_H_
#define SRC_GRAPHICS_LIB_COMPUTE_SPINEL_PLATFORMS_VK_DISPATCH_H_

//
//
//

#include <vulkan/vulkan_core.h>

#include "handle_pool.h"
#include "spinel_result.h"

//
// BACKGROUND:
//
//   Spinel paths and rasters can be long-lived.  A path or raster can
//   be created once and never disposed for the life of the Spinel
//   context.
//
//   Paths and rasters are defined through the public Spinel builder
//   APIs and are represented by opaque 32-bit handles.
//
//   A returned path or raster handle can immediately be used in
//   downstream dependent sub-APIs long before the path or raster is
//   actually materialized on the GPU.
//
//   This period where a path or raster hasn't been fully materialized
//   on the GPU but is referenced by a dependent sub-pipeline requires
//   that the handle's state be tracked so that happens-before
//   relationships are enforced.
//
// TIMELINE:
//
//   A Spinel "timeline" enforces happens-before relationships between
//   the path creation, raster creation and composition sub-pipelines.
//
//   The Spinel pipeline has two different sub-pipeline dependencies:
//
//     - PATHS-TO-RASTERS: Paths defined by the path builder have to
//       migrated to the GPU and packaged in a GPU-optimal coalesced
//       format before rasterization can begin.
//
//       The raster builder can immediately define rasterization
//       commands using path handles for unmaterialized paths.
//
//       But before the rasterization sub-pipeline can start, all path
//       dependencies must be resolved -- which may include flushing
//       path builders -- and the paths materialized.
//
//       Observe that there can be a one-to-many dependency between a
//       single path builder and more than one raster builders.
//
//     - RASTERS-TO-COMPOSITIONS: Rasters defined by the raster
//       builder are paths that have to be rasterized and
//       post-processed before placement into a composition.
//
//       A composition can immediately define place commands that
//       using raster handles for unmaterialized rasters.
//
//       But before the composition sub-pipeline can start, all raster
//       dependencies must be resolved -- which may include flushing
//       raster builders -- and the rasters materialized.
//
//       Observe that there can be a one-to-many dependency between a
//       single raster builder and more than one composition.
//
// CONSTRAINTS:
//
//   - One-to-many signalling isn't possible with VkSemaphores.
//
//   - Both VkSemapores and VkEvents must record a signal before
//     recording a wait.
//
//   - Allocating a Vulkan synchronization type per handle isn't
//     feasible.
//
//   - We would *prefer* to have as many driver-schedulable compute
//     shaders in flight as possible rather than have the host
//     explicitly manage the flow graph of dependencies because it
//     will add significant inter-submission latencies to the
//     pipeline. Note that this remains an option and was implemented
//     by earlier non-Vulkan implementations.
//
//   - Until Timeline Semaphores are available, the host will need to
//     explicitly schedule the task graph.
//
// OPERATION:
//
//   A "signaller" is:
//
//     - A path builder that defines a group of paths that are
//       dispatched to the GPU for processing. When the paths have
//       been materialized the dispatched group signals completion.
//
//     - A raster builder that defines a group of rasters that are
//       constructed from paths defined by a path builder and
//       dispatched to the GPU for processing.  When the rasters have
//       been materialized the dispatched group signals completion.
//
//  A "waiter" is:
//
//     - A raster builder that is waiting on one or more dispatched
//       groups of paths to materialize.
//
//     - A composition that is waiting on one or more dispatched
//       groups of rasters to materialize.
//
//  SIGNALLER:
//
//    1. A signaller works on a quantum of work called a "dispatch".
//
//    2. When a new dispatch is started, a dispatch id is acquired.
//
//    3. When the dispatch is complete, any registered handles are
//       marked complete and all waiters are signalled.
//
//  WAITER:
//
//    1. Before the waiter's dispatch is submitted, the waiter forces
//       all dependencies to be submitted.
//
//    2. The dependencies are determined by looking up each handle's
//       dispatch id in a table internal to the dispatch.
//
//    3. Each handle dispatch's signal list is updated with the waiter's
//       dispatch id.
//
//    4. When the waiter's dispatch is submitted, if the waiter's count
//       of signallers is zero then the dispatch is immediately
//       submitted to Vulkan.
//
//    5. If the waiter's count of signallers is greater than zero then
//       the dispatch is added to a wait list and won't be submitted to
//       Vulkan until signallers drive the wait count to zero.
//
// XXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXX
// X                                                                   X
// X FIXME(allanmac): This description needs to be updated.            X
// X                                                                   X
// X IMPLEMENTATION:                                                   X
// X                                                                   X
// X FIXME(allanmac): This description needs to be updated.            X
// X                                                                   X
// X The timeline implementation allows waiters to wait on signallers. X
// X                                                                   X
// X There are 3 blocks of state in a Spinel Vulkan device:            X
// X                                                                   X
// X - TIMELINE HANDLE EVENTS                                          X
// X                                                                   X
// X   The "timeline handle events" array that records which           X
// X   per-dispatch timeline event is associated with an               X
// X   unmaterialized handle.                                          X
// X                                                                   X
// X   Every handle has a one-byte timeline event that indexes an      X
// X   unspecified synchronization object or indicates that the handle X
// X   is materialized.                                                X
// X                                                                   X
// X   It's *critical* that we keep this primitive type *small*        X
// X   because there can be up to 2^27 handles.                        X
// X                                                                   X
// X   The event size could be increased to two bytes but the          X
// X   outstanding number of events should be limited to 1023.         X
// X                                                                   X
// X - TIMELINE                                                        X
// X                                                                   X
// X   The "paths timeline" and "rasters timeline" each manage a pool  X
// X   of timeline events.  Each timeline event indexes a Vulkan       X
// X   synchronization object and a pointer to a path or raster        X
// X   builder impl structure.                                         X
// X                                                                   X
// X   A one byte timeline event enables up to 254 active events.      X
// X                                                                   X
// X   There are currently no plans for supporting *more* than this    X
// X   number of events.                                               X
// X                                                                   X
// X   The target's config struct determines how many timeline events  X
// X   are internally allocated.                                       X
// X                                                                   X
// X   Note that there is a fixed-size pool of timeline events and if  X
// X   it's drained the Spinel context will block until in-flight work X
// X   has drained and events are returned to the pool.                X
// X                                                                   X
// X   This is OK but properly sizing the pool should make this a rare X
// X   occurrence.                                                     X
// X                                                                   X
// X   If it happens frequently it's simply an indication that the     X
// X   timeline pool is too small.                                     X
// X                                                                   X
// X   Future tooling, testing and logging will help us select good    X
// X   baseline resource allocations for the resources defined here,   X
// X   the various pools including the descriptor set pool sizes --    X
// X   it's a fairly easy process but we need to apply a heavy load to X
// X   the GPU to determine reasonable allocations to place in the     X
// X   vendor/arch-specific target_config structure.                   X
// X                                                                   X
// X - TIMELINE EVENT BITMAP                                           X
// X                                                                   X
// X   A per-dispatch "timeline event bitmap" that captures which      X
// X   timeline events are active or being waited upon by a downstream X
// X   sub-pipeline.                                                   X
// X                                                                   X
// X   A raster builder dispatch would capture a bitmap of all the     X
// X   active timeline events for the paths that it depends upon.      X
// X   When the dispatch completes, the active events are released.    X
// X                                                                   X
// X   A composition dispatch would capture a bitmap of all the active X
// X   timeline events for the rasters that it depends upon.  When the X
// X   dispatch completes, the active events are released.             X
// X                                                                   X
// XXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXX
//

typedef enum spn_dispatch_stage_e
{
  SPN_DISPATCH_STAGE_STATUS,
  SPN_DISPATCH_STAGE_BLOCK_POOL,
  SPN_DISPATCH_STAGE_PATH_BUILDER,
  SPN_DISPATCH_STAGE_RASTER_BUILDER_1,
  SPN_DISPATCH_STAGE_RASTER_BUILDER_2,
  SPN_DISPATCH_STAGE_COMPOSITION_RESET,
  SPN_DISPATCH_STAGE_COMPOSITION_PLACE,
  SPN_DISPATCH_STAGE_COMPOSITION_SEAL_1,
  SPN_DISPATCH_STAGE_COMPOSITION_SEAL_2,
  SPN_DISPATCH_STAGE_STYLING,
  SPN_DISPATCH_STAGE_RENDER,
  SPN_DISPATCH_STAGE_RECLAIM_PATHS,
  SPN_DISPATCH_STAGE_RECLAIM_RASTERS
} spn_dispatch_stage_e;

//
//
//

typedef uint8_t spn_dispatch_id_t;

//
//
// Use a pfn to submit the command buffer to a queue and signal a fence.
//
// NOTE(allanmac): this is only really used by the RENDER stage so it
// reinforces the idea of having per-stage dispatch id pools.
//
// All internal submissions use the default pfn.
//

typedef void (*spn_dispatch_submitter_pfn_t)(VkQueue               queue,
                                             VkFence               fence,
                                             VkCommandBuffer const cb,
                                             void *                data);
//
// Callback for submissions completion
//

typedef void (*spn_dispatch_completion_pfn_t)(void * payload);

//
// Supply a flushing function
//

typedef spn_result_t (*spn_dispatch_flush_pfn_t)(void * arg);

//
//
//

void
spn_device_dispatch_create(struct spn_device * const device);

void
spn_device_dispatch_dispose(struct spn_device * const device);

//
// Acquires a dispatch
//

spn_result_t
spn_device_dispatch_acquire(struct spn_device * const  device,
                            spn_dispatch_stage_e const stage,
                            spn_dispatch_id_t * const  id);

//
// Get/set dispatch attributes
//

VkCommandBuffer
spn_device_dispatch_get_cb(struct spn_device * const device, spn_dispatch_id_t const id);

void
spn_device_dispatch_set_submitter(struct spn_device * const          device,
                                  spn_dispatch_id_t const            id,
                                  spn_dispatch_submitter_pfn_t const submitter_pfn,
                                  void *                             submitter_data);

void *
spn_device_dispatch_set_completion(struct spn_device * const           device,
                                   spn_dispatch_id_t const             id,
                                   spn_dispatch_completion_pfn_t const completion_pfn,
                                   size_t const                        completion_payload_size);

void
spn_device_dispatch_set_flush_arg(struct spn_device * const device,
                                  spn_dispatch_id_t const   id,
                                  void *                    flush_arg);

void
spn_device_dispatch_reset_flush_arg(struct spn_device * const device, spn_dispatch_id_t const id);

//
// Register an unmaterialized handle with a WIP dispatch
//

void
spn_device_dispatch_register_handle(struct spn_device * const device,
                                    spn_dispatch_id_t const   id,
                                    spn_handle_t const        handle);

//
// Launch the dispatch
//

void
spn_device_dispatch_submit(struct spn_device * const device, spn_dispatch_id_t const id);

//
// Declare a dispatch happens-after another dispatch
//

void
spn_device_dispatch_happens_after(struct spn_device * const device,
                                  spn_dispatch_id_t const   id_after,
                                  spn_dispatch_id_t const   id_before);

//
// Declare a dispatch happens-after handles are materialized
//

void
spn_device_dispatch_happens_after_handles(struct spn_device * const      device,
                                          spn_dispatch_flush_pfn_t const flush_pfn,
                                          spn_dispatch_id_t const        id_after,
                                          spn_handle_t const * const     handles,
                                          uint32_t const                 size,
                                          uint32_t const                 span,
                                          uint32_t const                 head);

//
// Called after handles are materialized
//

void
spn_device_dispatch_handles_complete(struct spn_device * const  device,
                                     spn_handle_t const * const handles,
                                     uint32_t const             size,
                                     uint32_t const             span,
                                     uint32_t const             head);

//
//
//

#endif  // SRC_GRAPHICS_LIB_COMPUTE_SPINEL_PLATFORMS_VK_DISPATCH_H_
