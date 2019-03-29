// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

//
//
//

#include <string.h>
#include <assert.h>
#include <stdbool.h>

#include "grid.h"
#include "macros.h"
#include "runtime_cl_12.h"

//
// SPN grid dependencies can be represented with a DAG.
//
// This dependency graph may be modified to include some sort of block
// pool barrier to make block recovery explicit (and guaranteed safe).
//
//
//              PATH BUILDER
//                    |
//                    |
//                    |
//                    v
//             RASTER BUILDER
//                    |
//            +----+  |           +----+
//    Set Ops |    |  |           |    | Set Ops
//            |    v  v           v    |
//            +--COMPOSITION  STYLING--+
//                    |          |
//                    | +--------+
//                    | |
//                    v v
//                  SURFACE
//
//
//       STAGE                DEPENDENCIES
//  ==============    ============================
//  PATH BUILDER      -
//  RASTER BUILDER    PATH BUILDER
//  COMPOSITION       RASTER BUILDER, *COMPOSITION
//  STYLING           -, *STYLING
//  SURFACE           COMPOSITION, STYLING
//

//
// How many active grids can/should we have?
//
// FIXME -- we'll need to provide a small level of indirection if we
// want to support a much larger number of work-in-progress grids
//
// For now and for simplicity, unify all grid ids in one set.
//

typedef spn_uchar             spn_grid_id_t;  // 256 values

#define SPN_GRID_ID_INVALID   SPN_UCHAR_MAX   // 255 is invalid

#define SPN_GRID_SIZE_IDS     (SPN_GRID_ID_INVALID-1)
#define SPN_GRID_SIZE_DWORDS  ((SPN_GRID_SIZE_IDS+31)/32)

//
//
//

typedef enum spn_grid_state_e {

  SPN_GRID_STATE_READY,
  SPN_GRID_STATE_WAITING,
  SPN_GRID_STATE_FORCED,
  SPN_GRID_STATE_EXECUTING,
  SPN_GRID_STATE_COMPLETE,
  SPN_GRID_STATE_DETACHED,

  SPN_GRID_STATE_COUNT

} spn_grid_state_e;

//
//
//

struct spn_grid_pfn_name
{
  spn_grid_pfn pfn;
  char const * name;
};

//
//
//

struct spn_grid
{
  spn_grid_state_e          state;
  spn_uint                  id;

  struct spn_grid_deps    * deps;    // backpointer to deps
  void                  * * addr;    // pointer to invalidate

  void                    * data;

  struct spn_grid_pfn_name  waiting; // optional - if defined, typically used to yank the grid away from host
  struct spn_grid_pfn_name  execute; // optional - starts execution of waiting grid
  struct spn_grid_pfn_name  dispose; // optional - invoked when grid is complete

  struct {
    spn_uint                words[SPN_GRID_SIZE_DWORDS]; // 0:inactive, 1:active
    spn_uint                count;
  } before;

  struct {
    spn_uint                words[SPN_GRID_SIZE_DWORDS]; // 0:inactive, 1:active
    spn_uint                count;
  } after;
};

//
//
//

struct spn_grid_deps
{
  struct spn_runtime   * runtime;
  struct spn_scheduler * scheduler;

  spn_grid_id_t        * handle_map;

  struct spn_grid        grids [SPN_GRID_SIZE_IDS];   // deps + pfns + data
  spn_uint               active[SPN_GRID_SIZE_DWORDS]; // 1:inactive, 0:active

  spn_uint               count;                       // number of active ids
};

//
//
//

static
void
spn_grid_call(spn_grid_t const grid, struct spn_grid_pfn_name const * const pn)
{
  if (pn->pfn != NULL) {
    pn->pfn(grid);
  }
}

static
void
spn_grid_schedule(spn_grid_t const grid, struct spn_grid_pfn_name const * const pn)
{
  if (pn->pfn != NULL) {
    spn_scheduler_schedule(grid->deps->scheduler,pn->pfn,grid,pn->name);
  }
}

//
//
//

static
void
spn_grid_invalidate(spn_grid_t const grid)
{
  if (grid->addr != NULL) {
    *grid->addr = NULL;
  }
}

//
//
//

#if 0
spn_grid_t
spn_grid_move(spn_grid_t         const grid,
              spn_grid_state_e * const state,
              spn_grid_t       * const addr,
              void             * const data)
{
  spn_grid_invalidate(grid);

  grid->state = state;
  grid->addr  = addr;
  grid->data  = data;

  return grid;
}
#endif

void *
spn_grid_get_data(spn_grid_t const grid)
{
  return grid->data;
}

void
spn_grid_set_data(spn_grid_t const grid, void * const data)
{
  grid->data = data;
}

//
//
//

spn_grid_deps_t
spn_grid_deps_create(struct spn_runtime   * const runtime,
                     struct spn_scheduler * const scheduler,
                     spn_uint               const handle_pool_size)
{
  struct spn_grid_deps * const deps = spn_runtime_host_perm_alloc(runtime,SPN_MEM_FLAGS_READ_WRITE,sizeof(*deps));

  // save runtime
  deps->runtime    = runtime;
  deps->scheduler  = scheduler;

  size_t const handle_map_size = sizeof(*deps->handle_map) * handle_pool_size;

  // allocate handle map
  deps->handle_map = spn_runtime_host_perm_alloc(runtime,SPN_MEM_FLAGS_READ_WRITE,handle_map_size);

  // initialize handle map
  memset(deps->handle_map,0xFF,handle_map_size);

  // grids
  struct spn_grid * const grids = deps->grids;

#if 0 // DELETE ME LATER
  // initalize ids once -- could always infer id using offsetof()
  for (spn_uint id=0; id < SPN_GRID_SIZE_IDS; id++)
    grids[id].id = id;
#endif

  // mark all grids inactive except for last bit -- 1:inactive / 0:active
  for (spn_uint ii=0; ii < SPN_GRID_SIZE_DWORDS-1; ii++)
    deps->active[ii] = 0xFFFFFFFF;

  // last bit is marked active so that it is never allocated
  deps->active[SPN_GRID_SIZE_DWORDS-1] = 0x7FFFFFFF;

  // nothing active
  deps->count = 1;

  return deps;
}

void
spn_grid_deps_dispose(spn_grid_deps_t const deps)
{
  //
  // FIXME -- debug checks for active grids
  //
  spn_runtime_host_perm_free(deps->runtime,deps->handle_map);
  spn_runtime_host_perm_free(deps->runtime,deps);
}

//
//
//

#ifndef NDEBUG

void
spn_grid_deps_debug(struct spn_grid_deps const * const deps)
{
  fprintf(stderr,
          "00000000000000001111111111111111\n"
          "0123456789ABCDEF0123456789ABCDEF\n"
          "--------------------------------\n");

  for (spn_uint ii=0; ii<SPN_GRID_SIZE_DWORDS; ii++)
    {
      spn_uint const a = deps->active[ii];
      fprintf(stderr,
              "%1u%1u%1u%1u%1u%1u%1u%1u%1u%1u%1u%1u%1u%1u%1u%1u"
              "%1u%1u%1u%1u%1u%1u%1u%1u%1u%1u%1u%1u%1u%1u%1u%1u\n",
              (a>>0x00)&1,(a>>0x01)&1,(a>>0x02)&1,(a>>0x03)&1,
              (a>>0x04)&1,(a>>0x05)&1,(a>>0x06)&1,(a>>0x07)&1,
              (a>>0x08)&1,(a>>0x09)&1,(a>>0x0A)&1,(a>>0x0B)&1,
              (a>>0x0C)&1,(a>>0x0D)&1,(a>>0x0E)&1,(a>>0x0F)&1,
              (a>>0x10)&1,(a>>0x11)&1,(a>>0x12)&1,(a>>0x13)&1,
              (a>>0x14)&1,(a>>0x15)&1,(a>>0x16)&1,(a>>0x17)&1,
              (a>>0x18)&1,(a>>0x19)&1,(a>>0x1A)&1,(a>>0x1B)&1,
              (a>>0x1C)&1,(a>>0x1D)&1,(a>>0x1E)&1,(a>>0x1F)&1);
    }

  fprintf(stderr,"\n");
}

#endif

//
//
//

spn_grid_t
spn_grid_deps_attach(spn_grid_deps_t const deps,
                     spn_grid_t    * const addr,
                     void          * const data,
                     spn_grid_pfn          waiting_pfn,  // upon READY         > WAITING
                     spn_grid_pfn          execute_pfn,  // upon READY/WAITING > EXECUTING
                     spn_grid_pfn          dispose_pfn,  // upon EXECUTING     > COMPLETE
                     char    const * const waiting_name,
                     char    const * const execute_name,
                     char    const * const dispose_name)
{
  //
  // FIXME -- no more ids -- either fatal or flush & wait for grids to be released
  //
  // assert(deps->count < SPN_GRID_SIZE_IDS);
  //
  while (deps->count == SPN_GRID_SIZE_IDS)
    spn_scheduler_wait_one(deps->scheduler);

  // otherwise, an id exists so decrement count
  deps->count += 1;

  // find first set bit (1:inactive)
  spn_uint * active = deps->active;
  spn_uint   first  = 0;

  while (1)
    {
      spn_uint const idx = SPN_LZCNT_32(*active);

      first += idx;

      if (idx < 32)
        {
          // make inactive bit active: 1 -> 0
          *active &= ~(0x80000000 >> idx); // 0:active
          break;
        }

      // otherwise, inspect next word for inactive bit
      active += 1;
    }

  struct spn_grid * const grid = deps->grids + first;

  // save grid pointer
  if (addr != NULL)
    *addr = grid;

  // initialize elem
  *grid = (struct spn_grid){
    .state   = SPN_GRID_STATE_READY,
    .id      = first,
    .deps    = deps,
    .addr    = addr,
    .data    = data,
    .waiting = { .pfn = waiting_pfn, .name = waiting_name },
    .execute = { .pfn = execute_pfn, .name = execute_name },
    .dispose = { .pfn = dispose_pfn, .name = dispose_name },
    .before  = { { 0 }, 0 },
    .after   = { { 0 }, 0 }
  };

  return grid;
}

//
//
//

static
spn_bool
spn_grid_words_set(spn_uint ids[SPN_GRID_SIZE_DWORDS], spn_uint const id)
{
  spn_uint * const ptr  = ids + (id/32);
  spn_uint   const pre  = *ptr;
  spn_uint   const post = pre | (0x80000000 >> (id & 0x1F)); // set

  *ptr = post;

  return pre != post;
}

static
spn_bool
spn_grid_words_clear(spn_uint ids[SPN_GRID_SIZE_DWORDS], spn_uint const id)
{
  spn_uint * const ptr  = ids + (id/32);
  spn_uint   const pre  = *ptr;
  spn_uint   const post = pre & ~(0x80000000 >> (id & 0x1F)); // clear

  *ptr = post;

  return pre != post;
}

//
// we may want to allow the host to detach a grid
//

static
void
spn_grid_detach(spn_grid_t const grid)
{
  // for now make sure grid is complete
  // assert(grid->state == SPN_GRID_STATE_COMPLETE);

  // transition state
  grid->state = SPN_GRID_STATE_DETACHED;

  //
  // FIXME -- save profiling info
  //

  // cleanup
  if (spn_grid_words_set(grid->deps->active,grid->id)) // 1:inactive
    grid->deps->count -= 1;
}

//
//
//

void
spn_grid_map(spn_grid_t const grid, spn_handle_t const handle)
{
  grid->deps->handle_map[handle] = grid->id;
}

//
//
//

void
spn_grid_deps_force(spn_grid_deps_t      const deps,
                    spn_handle_t const * const handles,
                    spn_uint             const count)
{
  //
  // FIXME -- test to make sure handles aren't completely out of range integers
  //
  spn_grid_id_t * const handle_map = deps->handle_map;

  for (spn_uint ii=0; ii<count; ii++)
    {
      spn_grid_id_t grid_id = handle_map[SPN_TYPED_HANDLE_TO_HANDLE(handles[ii])];

      if (grid_id < SPN_GRID_ID_INVALID)
        {
          spn_grid_t const grid = deps->grids + grid_id;

          spn_grid_force(grid);

          while (grid->state >= SPN_GRID_STATE_COMPLETE)
            spn_scheduler_wait_one(deps->scheduler);
        }
    }
}

void
spn_grid_deps_unmap(spn_grid_deps_t      const deps,
                    spn_handle_t const * const handles,
                    spn_uint             const count)
{
  spn_grid_id_t * const handle_map = deps->handle_map;

  for (spn_uint ii=0; ii<count; ii++)
    handle_map[handles[ii]] = SPN_GRID_ID_INVALID;
}

//
// NOTE: We want this routine to be very very fast. The array of bit
// flags is probably as fast as we can go for a modest number of
// grids.
//
// NOTE: The before grid should never be NULL.  This means the grid's
// lifecycle should match the lifetime of the object it represents.
// This also means the grid "invalidation upon start" feature should
// be well understood before using it to clear the spn_grid_t.
//

void
spn_grid_happens_after_grid(spn_grid_t const after,
                            spn_grid_t const before)
{
  // declarations can't be made on non-ready grids
  assert(after->state == SPN_GRID_STATE_READY);

  if (before->state >= SPN_GRID_STATE_COMPLETE)
    return;

  if (spn_grid_words_set(after->before.words,before->id))
    after->before.count += 1;

  if (spn_grid_words_set(before->after.words,after->id))
    before->after.count += 1;
}

void
spn_grid_happens_after_handle(spn_grid_t const after, spn_handle_t const before)
{
  assert(after->state == SPN_GRID_STATE_READY);

  spn_uint const id_before = after->deps->handle_map[before];

  if (id_before >= SPN_GRID_ID_INVALID)
    return;

  if (spn_grid_words_set(after->before.words,id_before))
    after->before.count += 1;

  spn_grid_t const grid_before = after->deps->grids + id_before;

  if (spn_grid_words_set(grid_before->after.words,after->id))
    grid_before->after.count += 1;
}

//
// Remove dependency from grid
//

static
void
spn_grid_clear_dependency(spn_grid_t const after, spn_uint const before)
{
  spn_bool const is_change = spn_grid_words_clear(after->before.words,before);

  assert(is_change); // for now let's make sure this is a rising edge

  after->before.count -= 1;

  if ((after->before.count == 0) && ((after->state == SPN_GRID_STATE_WAITING) ||
                                     (after->state == SPN_GRID_STATE_FORCED)))
    {
      // schedule grid for execution
      after->state = SPN_GRID_STATE_EXECUTING;
      spn_grid_schedule(after,&after->execute);
    }
}

//
// Start the ready grid and wait for dependencies to complete
//

void
spn_grid_start(spn_grid_t const grid)
{
  // nothing to do if this grid isn't in a ready state
  if (grid->state != SPN_GRID_STATE_READY)
    return;

  // record transition through waiting state
  grid->state = SPN_GRID_STATE_WAITING;

  // the waiting pfn may be null -- e.g. the path builder
  // spn_grid_schedule(grid,&grid->waiting);
  spn_grid_call(grid,&grid->waiting);

  // clear the reference
  spn_grid_invalidate(grid);

  // execute if there are no dependencies
  if (grid->before.count == 0)
    {
      // tell grid it can execute
      grid->state = SPN_GRID_STATE_EXECUTING;
      spn_grid_schedule(grid,&grid->execute);
    }
}

//
// Start this grid and all its ready dependencies
//

void
spn_grid_force(spn_grid_t const grid)
{
  // return if this grid was forced, executing or complete
  if (grid->state >= SPN_GRID_STATE_FORCED)
    return;

  // if ready then move to waiting state
  if (grid->state == SPN_GRID_STATE_READY)
    {
      // tell grid to wait for execution
      grid->state = SPN_GRID_STATE_WAITING;

      // the waiting pfn may be null -- e.g. the path builder
      // spn_grid_schedule(grid,&grid->waiting);
      spn_grid_call(grid,&grid->waiting);

      // clear the reference
      spn_grid_invalidate(grid);
    }

  spn_uint before_count = grid->before.count;

  // if there are no grid dependencies then execute
  if (before_count == 0)
    {
      // tell grid it can execute
      grid->state = SPN_GRID_STATE_EXECUTING;
      spn_grid_schedule(grid,&grid->execute);
    }
  else // otherwise, start or make waiting all dependencies
    {
      grid->state = SPN_GRID_STATE_FORCED;

      struct spn_grid * before       = grid->deps->grids;
      spn_uint        * before_words = grid->before.words;
      spn_uint          active       = *before_words++;

      while (true)
        {
          // find first active
          spn_uint const idx = SPN_LZCNT_32(active);

          // no bits set so inspect next word
          if (idx == 32)
            {
              active  = *before_words++;
              before += 1;
              continue;
            }
          else // clear active
            {
              active       &= ~(0x80000000 >> idx);
              before_count -= 1;
            }

          // otherwise, force this elem with dependent
          spn_grid_force(before+idx);

          // no more bits?
          if (before_count == 0)
            break;
        }
    }
}

//
// Notify grids dependent on this grid that this grid is complete
//

void
spn_grid_complete(spn_grid_t const grid)
{
  // debug: grid was executing
  assert(grid->state == SPN_GRID_STATE_EXECUTING);

  // move grid to completion and dispose after notifying dependents
  grid->state = SPN_GRID_STATE_COMPLETE;

  spn_uint after_count = grid->after.count;

  if (after_count > 0)
    {
      // find set bits
      struct spn_grid * after       = grid->deps->grids;
      spn_uint        * after_words = grid->after.words;
      spn_uint          active      = *after_words++;

      while (true)
        {
          // find first active
          spn_uint const idx = SPN_LZCNT_32(active);

          // no bits set so inspect next word
          if (idx == 32)
            {
              active  = *after_words++;
              after  += 32;
              continue;
            }
          else // clear active
            {
              active      &= ~(0x80000000 >> idx);
              after_count -= 1;
            }

          // otherwise, clear this dependency
          spn_grid_clear_dependency(after+idx,grid->id);

          // no more bits?
          if (after_count == 0)
            break;
        }
    }

  // dispose of resources
  spn_grid_call(grid,&grid->dispose);

  // we don't need to hang on to this grid id any longer
  spn_grid_detach(grid);
}

///////////////////////////////////////////////////////////////////////////
//
// ALTERNATIVE IMPLEMENTATION WOULD SUPPORT A VARIABLE NUMBER OF
// ACTIVE GRIDS PER STAGE PRESUMABLY RESULTING IN SLIGHTLY LESS MEMORY
// USAGE.
//
// THE #1 OBJECTIVE OF THE GRID IMPLEMENTATION IS TO ENSURE THAT THE
// "HAPPENS-BEFORE" DECLARATION REMAINS ***FAST*** SINCE IT WILL BE
// CALLED FREQUENTLY.  THUS THE |GRIDS|^2 BIT ARRAY...
//
// WE DON'T NEED THIS RIGHT NOW...
//

#if 0

//
// For now, make them all the same size
//

#define SPN_GRID_STAGE_WORDS_PATH_BUILDER          SPN_GRID_MASK_WORDS
#define SPN_GRID_STAGE_WORDS_RASTER_BUILDER        SPN_GRID_MASK_WORDS
#define SPN_GRID_STAGE_WORDS_COMPOSITION           SPN_GRID_MASK_WORDS
#define SPN_GRID_STAGE_WORDS_STYLING               SPN_GRID_MASK_WORDS
#define SPN_GRID_STAGE_WORDS_SURFACE_COMPOSITION   SPN_GRID_MASK_WORDS
#define SPN_GRID_STAGE_WORDS_SURFACE_STYLING       SPN_GRID_MASK_WORDS

//
//
//

typedef enum spn_grid_stage_type {

  SPN_GRID_STAGE_TYPE_PATH_BUILDER,
  SPN_GRID_STAGE_TYPE_RASTER_BUILDER,
  SPN_GRID_STAGE_TYPE_COMPOSITION,
  SPN_GRID_STAGE_TYPE_STYLING,
  SPN_GRID_STAGE_TYPE_SURFACE_COMPOSITION,
  SPN_GRID_STAGE_TYPE_SURFACE_STYLING,

  SPN_GRID_STAGE_TYPE_COUNT

} spn_grid_stage_type;

//
//
//

union spn_grid_id
{
  spn_grid_id_t u32;

  struct {
    spn_ushort  index;
    spn_ushort  stage;
  };
}

SPN_STATIC_ASSERT(sizeof(union spn_grid_id) == sizeof(spn_uint));

//
//
//

#endif

//
//
//
