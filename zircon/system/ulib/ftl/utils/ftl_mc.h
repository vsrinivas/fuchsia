// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <stdlib.h>

#include <zircon/compiler.h>

#include "ftl_private.h"

/***********************************************************************/
/* Type Declarations                                                   */
/***********************************************************************/
// Cache miss read/write interface
typedef int (*ftlmcFuncW)(void* ftl, ui32 mpn, void* buf);
typedef int (*ftlmcFuncR)(void* ftl, ui32 mpn, void* buf, int* unmapped);

// Cache Entry Type
typedef struct ftlmc_cache_entry ftlmcEntry;

// Cache Type
typedef struct {
  ftlmcEntry* entry;      // array containing all cache entries
  ftlmcEntry** hash_tbl;  // hash table used to index cached pages
  CircLink lru_list;      // least recently used entry list
  void* ftl;              // handle to FTL volume using cache
  ftlmcFuncW write;       // write function on cache miss
  ftlmcFuncR read;        // read function on cache miss
  ui32 num_mpgs;          // number of cached map pages
  ui32 num_dirty;         // number of dirty cached entries
  ui32 mpg_sz;            // size of a cached map page in bytes
} FTLMC;

__BEGIN_CDECLS

/***********************************************************************/
/* Function Prototypes                                                 */
/***********************************************************************/
FTLMC* ftlmcNew(void* ftl_ndm, ui32 cache_size, ftlmcFuncW wf, ftlmcFuncR rf, ui32 mpg_sz);
void ftlmcDelete(FTLMC** ftlmc_ptr);
void ftlmcInit(FTLMC* ftlmc);
void* ftlmcGetPage(FTLMC* ftlmc, ui32 mpn, int* new_map);
int ftlmcFlushPage(FTLMC* ftlmc, ui32 mpn);
int ftlmcFlushMap(FTLMC* ftlmc);
ui32* ftlmcInCache(FTLMC* ftlmc, ui32 mpn);
ui32 ftlmcRAM(const FTLMC* ftlmc);

__END_CDECLS
