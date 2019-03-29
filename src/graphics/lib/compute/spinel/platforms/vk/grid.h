// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

//
//
//

#include "handle.h"
#include "scheduler.h"

//
// The requirement is that every grid struct begin with an spn_grid_t
//

typedef struct spn_grid      * spn_grid_t;
typedef struct spn_grid_deps * spn_grid_deps_t;

//
//
//

typedef void (* spn_grid_pfn)(spn_grid_t const grid);

//
//
//

#define SPN_IS_GRID_INVALID(grid)  (grid == NULL)

//
//
//

#define SPN_GRID_DEPS_ATTACH(deps,addr,data,waiting_pfn,execute_pfn,dispose_pfn) \
  spn_grid_deps_attach(deps,addr,data,                                  \
                       waiting_pfn,execute_pfn,dispose_pfn,             \
                       #waiting_pfn,#execute_pfn,#dispose_pfn)          \
//
//
//

spn_grid_deps_t
spn_grid_deps_create(struct spn_runtime   * const runtime,
                     struct spn_scheduler * const scheduler,
                     spn_uint               const handle_pool_size);

void
spn_grid_deps_dispose(spn_grid_deps_t const deps);

//
//
//

#ifndef NDEBUG
void
spn_grid_deps_debug(struct spn_grid_deps const * const deps);
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
                     char    const * const dispose_name);

#if 0
//
// Not using this yet -- needs to properly detach and reclaim a ready
// grid's resources
//
void
spn_grid_detach(spn_grid_t const grid);
#endif

//
//
//

void *
spn_grid_get_data(spn_grid_t const grid);

void
spn_grid_set_data(spn_grid_t const grid, void * const data);

//
//
//

void
spn_grid_map(spn_grid_t const grid, spn_handle_t const handle);

//
//
//

void
spn_grid_deps_force(spn_grid_deps_t      const deps,
                    spn_handle_t const * const handles,
                    spn_uint             const count);

void
spn_grid_deps_unmap(spn_grid_deps_t      const deps,
                    spn_handle_t const * const handles,
                    spn_uint             const count);

//
//
//

void
spn_grid_happens_after_grid(spn_grid_t const after,
                            spn_grid_t const before);

void
spn_grid_happens_after_handle(spn_grid_t   const after,
                              spn_handle_t const before);

//
// should be called by host
//

void
spn_grid_start(spn_grid_t const grid);

void
spn_grid_force(spn_grid_t const grid);

//
// should be called by the scheduler
//

void
spn_grid_complete(spn_grid_t const grid);

//
//
//

#if 0
//
// delete when ready
//
spn_grid_t
spn_grid_move(spn_grid_t         const grid,
              spn_grid_state_e * const state,
              spn_grid_t       * const addr,
              void             * const data);
#endif

//
//
//
