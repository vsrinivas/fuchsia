// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//
//
//

#if 0
#include <stdlib.h>
#include <stdio.h>

#include "hs/cl/hs_cl.h"

#include "common/cl/assert_cl.h"

#include "composition_cl_12.h"
#include "config_cl.h"

#include "context.h"
#include "raster.h"
#include "handle.h"

#include "runtime_cl_12.h"

#include "common.h"
#include "tile.h"
#endif

//
// COMPOSITION PLACE
//
// This is a snapshot of the host-side command queue.
//
// Note that the composition command extent could be implemented as
// either a mapped buffer or simply copied to an ephemeral extent.
//
// This implementation may vary between compute platforms.
//

struct spn_composition_place
{
  struct spn_composition_impl      * impl;

  cl_command_queue                   cq;

  struct spn_extent_phw1g_tdrNs_snap cmds;

  spn_subbuf_id_t                    id;
};

//
// composition states
//

typedef enum spn_composition_state_e {

  SPN_COMPOSITION_STATE_UNSEALING,
  SPN_COMPOSITION_STATE_UNSEALED,
  SPN_COMPOSITION_STATE_SEALING,
  SPN_COMPOSITION_STATE_SEALED

} spn_composition_state_e;

//
// IMPL
//

struct spn_composition_impl
{
  struct spn_composition        * composition;
  struct spn_runtime            * runtime;

  SPN_ASSERT_STATE_DECLARE(spn_composition_state_e);

  spn_int                         lock_count; // wip renders

  struct {
    spn_grid_t                    place;
    spn_grid_t                    sort;
  } grids;

  cl_command_queue                cq;

  struct {
    cl_kernel                     place;
    cl_kernel                     segment;
  } kernels;

  // raster ids must be held until the composition is reset or
  // released and then their refcounts can be decremented
  struct {
    struct spn_extent_phrw        extent;
    uint32_t                      count;
  } saved;

  struct {
    struct spn_extent_ring        ring;   // how many slots left?
    struct spn_extent_phw1g_tdrNs extent; // wip command extent
  } cmds;

  // composition extent length
  struct spn_extent_phr_pdrw      atomics;

  // composition ttck extent
  struct spn_extent_pdrw          keys;

  // key offsets in sealed and sorted ttck extent
  struct spn_extent_pdrw          offsets;
};

//
// ATOMICS
//

struct spn_place_atomics
{
  uint32_t keys;
  uint32_t offsets;
};

//
// Forward declarations
//

static
void
spn_composition_unseal_block(struct spn_composition_impl * const impl,
                             spn_bool                      const block);

//
//
//

static
void
spn_composition_pfn_release(struct spn_composition_impl * const impl)
{
  if (--impl->composition->ref_count != 0)
    return;

  //
  // otherwise, dispose of all resources
  //

  // the unsealed state is a safe state to dispose of resources
  spn_composition_unseal_block(impl,true); // block

  struct spn_runtime * const runtime = impl->runtime;

  // free host composition
  spn_runtime_host_perm_free(runtime,impl->composition);

  // release the cq
  spn_runtime_release_cq_in_order(runtime,impl->cq);

  // release kernels
  cl(ReleaseKernel(impl->kernels.place));
  cl(ReleaseKernel(impl->kernels.segment));

  // release extents
  spn_extent_phw1g_tdrNs_free(runtime,&impl->cmds.extent);
  spn_extent_phrw_free       (runtime,&impl->saved.extent);
  spn_extent_phr_pdrw_free   (runtime,&impl->atomics);

  spn_extent_pdrw_free       (runtime,&impl->keys);
  spn_extent_pdrw_free       (runtime,&impl->offsets);

  // free composition impl
  spn_runtime_host_perm_free(runtime,impl);
}

//
//
//

static
void
spn_composition_place_grid_pfn_dispose(spn_grid_t const grid)
{
  struct spn_composition_place * const place   = spn_grid_get_data(grid);
  struct spn_composition_impl  * const impl    = place->impl;
  struct spn_runtime           * const runtime = impl->runtime;

  // release cq
  spn_runtime_release_cq_in_order(runtime,place->cq);

  // unmap the snapshot (could be a copy)
  spn_extent_phw1g_tdrNs_snap_free(runtime,&place->cmds);

  // release place struct
  spn_runtime_host_temp_free(runtime,place,place->id);

  // release impl
  spn_composition_pfn_release(impl);
}

//
//
//

static
void
spn_composition_place_read_complete(spn_grid_t const grid)
{
  spn_grid_complete(grid);
}

static
void
spn_composition_place_read_cb(cl_event event, cl_int status, spn_grid_t const grid)
{
  SPN_CL_CB(status);

  struct spn_composition_place * const place     = spn_grid_get_data(grid);
  struct spn_composition_impl  * const impl      = place->impl;
  struct spn_runtime           * const runtime   = impl->runtime;
  struct spn_scheduler         * const scheduler = runtime->scheduler;

  // as quickly as possible, enqueue next stage in pipeline to context command scheduler
  SPN_SCHEDULER_SCHEDULE(scheduler,spn_composition_place_read_complete,grid);
}

static
void
spn_composition_place_grid_pfn_execute(spn_grid_t const grid)
{
  //
  // FILLS EXPAND
  //
  // need result of cmd counts before launching RASTERIZE grids
  //
  // - OpenCL 1.2: copy atomic counters back to host and launch RASTERIZE grids from host
  // - OpenCL 2.x: have a kernel size and launch RASTERIZE grids from device
  // - or launch a device-wide grid that feeds itself but that's unsatisfying
  //
  struct spn_composition_place * const place   = spn_grid_get_data(grid);
  struct spn_composition_impl  * const impl    = place->impl;
  struct spn_runtime           * const runtime = impl->runtime;

  uint32_t  const work_size = spn_extent_ring_snap_count(place->cmds.snap);
  uint32_t4 const clip      = { 0, 0, UINT32_T_MAX, UINT32_T_MAX };

  // initialize kernel args
  cl(SetKernelArg(impl->kernels.place,0,SPN_CL_ARG(impl->runtime->block_pool.blocks.drw)));
  cl(SetKernelArg(impl->kernels.place,1,SPN_CL_ARG(impl->atomics.drw)));
  cl(SetKernelArg(impl->kernels.place,2,SPN_CL_ARG(impl->keys.drw)));
  cl(SetKernelArg(impl->kernels.place,3,SPN_CL_ARG(place->cmds.drN)));
  cl(SetKernelArg(impl->kernels.place,4,SPN_CL_ARG(runtime->handle_pool.map.drw)));
  cl(SetKernelArg(impl->kernels.place,5,SPN_CL_ARG(clip))); // FIXME -- convert the clip to yx0/yx1 format
  cl(SetKernelArg(impl->kernels.place,6,SPN_CL_ARG(work_size)));

  // launch kernel
  spn_device_enqueue_kernel(runtime->device,
                            SPN_DEVICE_KERNEL_ID_PLACE,
                            place->cq,
                            impl->kernels.place,
                            work_size,
                            0,NULL,NULL);
  //
  // copy atomics back after every place launch
  //
  cl_event complete;

  spn_extent_phr_pdrw_read(&impl->atomics,place->cq,&complete);

  cl(SetEventCallback(complete,CL_COMPLETE,spn_composition_place_read_cb,grid));
  cl(ReleaseEvent(complete));

  // flush command queue
  cl(Flush(place->cq));
}

//
//
//

static
void
spn_composition_snap(struct spn_composition_impl * const impl)
{
  spn_composition_retain(impl->composition);

  spn_subbuf_id_t id;

  struct spn_composition_place * const place = spn_runtime_host_temp_alloc(impl->runtime,
                                                                           SPN_MEM_FLAGS_READ_WRITE,
                                                                           sizeof(*place),&id,NULL);

  // save the subbuf id
  place->id = id;

  // save backpointer
  place->impl = impl;

  // set grid data
  spn_grid_set_data(impl->grids.place,place);

  // acquire command queue
  place->cq = spn_runtime_acquire_cq_in_order(impl->runtime);

  // checkpoint the ring
  spn_extent_ring_checkpoint(&impl->cmds.ring);

  // make a snapshot
  spn_extent_phw1g_tdrNs_snap_init(impl->runtime,&impl->cmds.ring,&place->cmds);

  // unmap the snapshot (could be a copy)
  spn_extent_phw1g_tdrNs_snap_alloc(impl->runtime,
                                    &impl->cmds.extent,
                                    &place->cmds,
                                    place->cq,
                                    NULL);

  spn_grid_force(impl->grids.place);
}

//
//
//

static
void
spn_composition_pfn_seal(struct spn_composition_impl * const impl)
{
  // return if sealing or sealed
  if (impl->state >= SPN_COMPOSITION_STATE_SEALING)
    return;

  struct spn_runtime   * const runtime   = impl->runtime;
  struct spn_scheduler * const scheduler = runtime->scheduler;

  //
  // otherwise, wait for UNSEALING > UNSEALED transition
  //
  if (impl->state == SPN_COMPOSITION_STATE_UNSEALING)
    {
      SPN_SCHEDULER_WAIT_WHILE(scheduler,impl->state != SPN_COMPOSITION_STATE_UNSEALED);
    }
  else // or we were already unsealed
    {
      // flush is there is work in progress
      uint32_t const count = spn_extent_ring_wip_count(&impl->cmds.ring);

      if (count > 0) {
        spn_composition_snap(impl);
      }
    }

  //
  // now unsealed so we need to start sealing...
  //
  impl->state = SPN_COMPOSITION_STATE_SEALING;

  //
  // the seal operation implies we should force start all dependencies
  // that are still in a ready state
  //
  spn_grid_force(impl->grids.sort);
}

//
//
//

void
spn_composition_sort_execute_complete(struct spn_composition_impl * const impl)
{
  // we're sealed
  impl->state = SPN_COMPOSITION_STATE_SEALED;

  // this grid is done
  spn_grid_complete(impl->grids.sort);
}

static
void
spn_composition_sort_execute_cb(cl_event event, cl_int status, struct spn_composition_impl * const impl)
{
  SPN_CL_CB(status);

  // as quickly as possible, enqueue next stage in pipeline to context command scheduler
  SPN_SCHEDULER_SCHEDULE(impl->runtime->scheduler,spn_composition_sort_execute_complete,impl);
}

static
void
spn_composition_sort_grid_pfn_execute(spn_grid_t const grid)
{
  struct spn_composition_impl * const impl    = spn_grid_get_data(grid);
  struct spn_runtime          * const runtime = impl->runtime;

  // we should be sealing
  assert(impl->state == SPN_COMPOSITION_STATE_SEALING);

  struct spn_place_atomics * const atomics = impl->atomics.hr;

#ifndef NDEBUG
  fprintf(stderr,"composition sort: %u\n",atomics->keys);
#endif

  if (atomics->keys > 0)
    {
      uint32_t keys_padded_in, keys_padded_out;

      hs_cl_pad(runtime->hs,atomics->keys,&keys_padded_in,&keys_padded_out);

      hs_cl_sort(impl->runtime->hs,
                 impl->cq,
                 0,NULL,NULL,
                 impl->keys.drw,
                 NULL,
                 atomics->keys,
                 keys_padded_in,
                 keys_padded_out,
                 false);

      cl(SetKernelArg(impl->kernels.segment,0,SPN_CL_ARG(impl->keys.drw)));
      cl(SetKernelArg(impl->kernels.segment,1,SPN_CL_ARG(impl->offsets.drw)));
      cl(SetKernelArg(impl->kernels.segment,2,SPN_CL_ARG(impl->atomics.drw)));

      // find start of each tile
      spn_device_enqueue_kernel(runtime->device,
                                SPN_DEVICE_KERNEL_ID_SEGMENT_TTCK,
                                impl->cq,
                                impl->kernels.segment,
                                atomics->keys,
                                0,NULL,NULL);
    }

  cl_event complete;

  // next stage needs to know number of key segments
  spn_extent_phr_pdrw_read(&impl->atomics,impl->cq,&complete);

  // register a callback
  cl(SetEventCallback(complete,CL_COMPLETE,spn_composition_sort_execute_cb,impl));
  cl(ReleaseEvent(complete));

  // flush cq
  cl(Flush(impl->cq));
}

//
//
//

static
void
spn_composition_raster_release(struct spn_composition_impl * const impl)
{
  //
  // reference counts to rasters can only be released when the
  // composition is unsealed and the atomics are reset.
  //
  spn_runtime_raster_device_release(impl->runtime,
                                    impl->saved.extent.hrw,
                                    impl->saved.count);
  // reset count
  impl->saved.count = 0;
}

//
//
//

static
void
spn_composition_unseal_block(struct spn_composition_impl * const impl,
                             spn_bool                      const block)
{
  // return if already unsealed
  if (impl->state == SPN_COMPOSITION_STATE_UNSEALED)
    return;

  //
  // otherwise, we're going to need to pump the scheduler
  //
  struct spn_scheduler * const scheduler = impl->runtime->scheduler;

  //
  // wait for UNSEALING > UNSEALED transition
  //
  if (impl->state == SPN_COMPOSITION_STATE_UNSEALING)
    {
      if (block) {
        SPN_SCHEDULER_WAIT_WHILE(scheduler,impl->state != SPN_COMPOSITION_STATE_UNSEALED);
      }
      return;
    }

  //
  // wait for SEALING > SEALED transition ...
  //
  if (impl->state == SPN_COMPOSITION_STATE_SEALING)
    {
      // wait if sealing
      SPN_SCHEDULER_WAIT_WHILE(scheduler,impl->state != SPN_COMPOSITION_STATE_SEALED);
    }

  // wait for rendering locks to be released
  SPN_SCHEDULER_WAIT_WHILE(scheduler,impl->lock_count > 0);

  //
  // no need to visit UNSEALING state with this implementation
  //

  // acquire a new grid
  impl->grids.sort = SPN_GRID_DEPS_ATTACH(impl->runtime->deps,
                                          NULL,  // the composition state guards this
                                          impl,
                                          NULL,  // no waiting
                                          spn_composition_sort_grid_pfn_execute,
                                          NULL); // no dispose

  // mark composition as unsealed
  impl->state = SPN_COMPOSITION_STATE_UNSEALED;
}

//
// can only be called on a composition that was just unsealed
//
static
void
spn_composition_reset(struct spn_composition_impl * const impl)
{
  // zero the atomics
  spn_extent_phr_pdrw_zero(&impl->atomics,impl->cq,NULL);

  // flush it
  cl(Flush(impl->cq));

  // release all the rasters
  spn_composition_raster_release(impl);
}

static
void
spn_composition_unseal_block_reset(struct spn_composition_impl * const impl,
                                   spn_bool                      const block,
                                   spn_bool                      const reset)
{
  spn_composition_unseal_block(impl,block);

  if (reset) {
    spn_composition_reset(impl);
  }
}

//
//
//

static
void
spn_composition_pfn_unseal(struct spn_composition_impl * const impl, spn_bool const reset)
{
  spn_composition_unseal_block_reset(impl,false,reset);
}

//
// only needs to create a grid
//

static
void
spn_composition_place_create(struct spn_composition_impl * const impl)
{
  // acquire a grid
  impl->grids.place = SPN_GRID_DEPS_ATTACH(impl->runtime->deps,
                                           &impl->grids.place,
                                           NULL,
                                           NULL, // no waiting
                                           spn_composition_place_grid_pfn_execute,
                                           spn_composition_place_grid_pfn_dispose);

  // assign happens-after relationship
  spn_grid_happens_after_grid(impl->grids.sort,impl->grids.place);
}


static
spn_result
spn_composition_pfn_place(struct spn_composition_impl * const impl,
                          spn_raster_t          const *       rasters,
                          spn_layer_id          const *       layer_ids,
                          float                 const *       txs,
                          float                 const *       tys,
                          uint32_t                         count)
{
  // block and yield if not unsealed
  spn_composition_unseal_block(impl,true);

  //
  // validate and retain all rasters
  //
  spn_result err;

  err = spn_runtime_handle_device_validate_retain(impl->runtime,
                                                  SPN_TYPED_HANDLE_TYPE_IS_RASTER,
                                                  rasters,
                                                  count);
  if (err)
    return err;

  spn_runtime_handle_device_retain(impl->runtime,rasters,count);

  //
  // save the stripped handles
  //
  spn_raster_t * saved = impl->saved.extent.hrw;

  saved             += impl->saved.count;
  impl->saved.count += count;

  for (uint32_t ii=0; ii<count; ii++) {
    saved[ii] = SPN_TYPED_HANDLE_TO_HANDLE(*rasters++);
  }

  //
  // - declare the place grid happens after the raster
  // - copy place commands into ring
  //
  do {
    uint32_t rem;

    // find out how much room is left in then ring's snap
    // if the place ring is full -- let it drain
    SPN_SCHEDULER_WAIT_WHILE(impl->runtime->scheduler,(rem = spn_extent_ring_wip_rem(&impl->cmds.ring)) == 0);

    // append commands
    uint32_t avail = min(rem,count);

    // decrement count
    count -= avail;

    // launch a place kernel after copying commands?
    spn_bool const is_wip_full = (avail == rem);

    // if there is no place grid then create one
    if (impl->grids.place == NULL)
      {
        spn_composition_place_create(impl);
      }

    //
    // FIXME -- OPTIMIZATION? -- the ring_wip_index_inc() test can
    // be avoided by splitting into at most two intervals. It should
    // be plenty fast as is though so leave for now.
    //
    union spn_cmd_place * const cmds = impl->cmds.extent.hw1;

    if ((txs == NULL) && (tys == NULL))
      {
        while (avail-- > 0)
          {
            spn_raster_t const raster = *saved++;

            spn_grid_happens_after_handle(impl->grids.place,raster);

            cmds[spn_extent_ring_wip_index_inc(&impl->cmds.ring)] =
              (union spn_cmd_place){ raster, *layer_ids++, 0, 0 };
          }
      }
    else if (txs == NULL)
      {
        while (avail-- > 0)
          {
            spn_raster_t const raster = *saved++;

            spn_grid_happens_after_handle(impl->grids.place,raster);

            cmds[spn_extent_ring_wip_index_inc(&impl->cmds.ring)] =
              (union spn_cmd_place){ raster,
                                     *layer_ids++,
                                     0,
                                     SPN_PLACE_CMD_TY_CONVERT(*tys++) };
          }
      }
    else if (tys == NULL)
      {
        while (avail-- > 0)
          {
            spn_raster_t const raster = *saved++;

            spn_grid_happens_after_handle(impl->grids.place,raster);

            cmds[spn_extent_ring_wip_index_inc(&impl->cmds.ring)] =
              (union spn_cmd_place){ raster,
                                     *layer_ids++,
                                     SPN_PLACE_CMD_TX_CONVERT(*txs++),
                                     0 };
          }
      }
    else
      {
        while (avail-- > 0)
          {
            spn_raster_t const raster = *saved++;

            spn_grid_happens_after_handle(impl->grids.place,raster);

            cmds[spn_extent_ring_wip_index_inc(&impl->cmds.ring)] =
              (union spn_cmd_place){ raster,
                                     *layer_ids++,
                                     SPN_PLACE_CMD_TX_CONVERT(*txs++),
                                     SPN_PLACE_CMD_TY_CONVERT(*tys++) };
          }
      }

    // launch place kernel?
    if (is_wip_full) {
      spn_composition_snap(impl);
    }
  } while (count > 0);

  return SPN_SUCCESS;
}

//
//
//

static
void
spn_composition_get_bounds(struct spn_composition_impl * const impl, spn_int bounds[4])
{
  //
  // FIXME -- not implemented yet
  //
  // impl bounds will be copied back after sealing
  //
  bounds[0] = SPN_INT_MIN;
  bounds[1] = SPN_INT_MIN;
  bounds[2] = SPN_INT_MAX;
  bounds[3] = SPN_INT_MAX;
}

//
//
//

void
spn_composition_retain_and_lock(struct spn_composition * const composition)
{
  spn_composition_retain(composition);

  composition->impl->lock_count += 1;
}

void
spn_composition_unlock_and_release(struct spn_composition * const composition)
{
  composition->impl->lock_count -= 1;

  spn_composition_pfn_release(composition->impl);
}

//
//
//

spn_result
spn_composition_impl_create(struct spn_device * const device,
                            spn_composition_t * const composition)
{
  //
  // retain the context
  // spn_context_retain(context);
  //
  struct spn_allocator_host_perm * const perm = &device->allocator.host.perm;

  //
  // allocate impl
  //
  struct spn_composition_impl * const impl =
    spnallocator_host_perm_alloc(perm,
                                 SPN_MEM_FLAGS_READ_WRITE,
                                 sizeof(*impl));

  //
  // allocate composition
  //
  struct spn_composition * const comp =
    spn_allocator_host_perm_alloc(perm,
                                  SPN_MEM_FLAGS_READ_WRITE,
                                  sizeof(*comp));

  *composition      = comp;
  impl->composition = comp;
  comp->impl        = impl;

  comp->place       = spn_ci_place;
  comp->seal        = spn_ci_seal;
  comp->unseal      = spn_ci_unseal;
  comp->bounds      = spn_ci_get_bounds;
  comp->release     = spn_ci_release;

  impl->lock_count  = 0;

  // get config
  struct spn_config const * const config = runtime->config;

  // initialize ring size with config values
  spn_extent_ring_init(&impl->cmds.ring,
                       config->composition.cmds.elem_count,
                       config->composition.cmds.snap_count,
                       sizeof(union spn_cmd_place));

  spn_extent_phw1g_tdrNs_alloc(runtime,&impl->cmds.extent ,sizeof(union spn_cmd_place) * config->composition.cmds.elem_count);
  spn_extent_phrw_alloc       (runtime,&impl->saved.extent,sizeof(spn_raster_t)        * config->composition.raster_ids.elem_count);
  spn_extent_phr_pdrw_alloc   (runtime,&impl->atomics     ,sizeof(struct spn_place_atomics));

  spn_extent_pdrw_alloc       (runtime,&impl->keys        ,sizeof(spn_ttxk_t)          * config->composition.keys.elem_count);
  spn_extent_pdrw_alloc       (runtime,&impl->offsets     ,sizeof(uint32_t)            * (1u << SPN_TTCK_HI_BITS_YX)); // 1MB

  // nothing saved
  impl->saved.count = 0;

  //
  // init refcount & state
  //
  comp->ref_count = 1;

  SPN_ASSERT_STATE_INIT(impl,SPN_COMPOSITION_STATE_SEALED);

  // unseal the composition, zero the atomics, etc.
  spn_composition_unseal_block_reset(impl,false,true);

  return SPN_SUCCESS;
}

//
//
//
