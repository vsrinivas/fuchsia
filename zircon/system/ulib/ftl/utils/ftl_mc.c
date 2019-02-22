// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <sys.h>
#include "ftl_mc.h"
#include <fsprivate.h>

// Configuration
#define MC_DEBUG FALSE

// Symbol Definitions
// States allowed for a cached MPN
#define FTLMC_CLEAN 0
#define FTLMC_DIRTY 1

// Type Declarations
struct ftlmc_cache_entry {
    CircLink lru_link;     // least recently used list entry
    ftlmcEntry* next_hash; // hash bucket list is double linked list
    ftlmcEntry* prev_hash;
    ftlmcEntry** hash_head; // pointer to head of hash table list
    ui32* data;             // pointer to map page contents
    ui32 mpn;               // map page number
    ui16 status;            // status of page - dirty/clean
};

// Local Function Definitions

//        hash: Hash based on the page number
//
//      Inputs: mpn = map page number to be hashed
//              num_mpgs = size of hash table in number of map pages
//
//     Returns: Index into the hash table where value gets hashed
//
static ui32 hash(ui32 mpn, ui32 num_mpgs) {
    return (19823u * mpn + 321043U) % num_mpgs;
}

#if MC_DEBUG
// check_cache: Debug function to check cache consistency
//
static void check_cache(FTLMC* cache, ui32 mpn) {
    int count = 0;
    ftlmcEntry* entry;
    CircLink* link;

    // Loop over map page cache LRU list.
    for (link = CIRC_LIST_HEAD(&cache->lru_list);; link = link->next_bck) {
        // Break at list end.
        if (CIRC_LIST_AT_END(link, &cache->lru_list))
            break;

        // Convert 'lru_link' pointer to entry pointer.
        entry = (ftlmcEntry*)((ui8*)link - offsetof(ftlmcEntry, lru_link));

        // Check if entry is used.
        if (entry->mpn != (ui32)-1) {
            if (entry->hash_head != &cache->hash_tbl[hash(entry->mpn, cache->num_mpgs)]) {
                printf("\nFTL MAP CACHE: mpn = %u hash_head != hash()\n", mpn);
                exit(-1);
            }
            if (entry->hash_head[0] == NULL) {
                printf("\nFTL MAP CACHE: mpn = %u hash_head is NULL!\n", mpn);
                exit(-1);
            }
        }
    }

    if (count > 1) {
        printf("\nFTL MAP CACHE: mpn = %u is cached %d times\n", mpn, count);
        exit(-1);
    }
}
#endif // MC_DEBUG

// Global Function Definitions

//    ftlmcRAM: Return RAM used FTL cache
//
//       Input: cache = cache handle or NULL
//
//     Returns: RAM usage in bytes
//
ui32 ftlmcRAM(const FTLMC* cache) {
    return cache
               ? sizeof(FTLMC) +
                     cache->num_mpgs * (sizeof(ftlmcEntry) + cache->mpg_sz + sizeof(ftlmcEntry*))
               : 0;
}

//    ftlmcNew: Create a new instance of an FTL map pages cache
//
//      Inputs: ftl = handle to FTL using this cache
//              num_mpgs = number of map pages the cache holds
//              wf = write map page function in case of cache miss
//              rf = read map page function in case of cache miss
//              mpg_sz = map page size in bytes
//
//     Returns: Handle to new cache if successful, NULL on error
//
FTLMC* ftlmcNew(void* ftl, ui32 num_mpgs, ftlmcFuncW wf, ftlmcFuncR rf, ui32 mpg_sz) {
    FTLMC* cache;
    ui32* data_space;

    // Allocate space to hold the cache structure. Return NULL if unable.
    cache = FsCalloc(1, sizeof(FTLMC));
    if (cache == NULL)
        return NULL;

    // Set the cache FTL handle and read and write functions.
    cache->ftl = ftl;
    cache->read_TFS = rf;
    cache->write_TFS = wf;

    // Set the number of pages and their size.
    cache->num_mpgs = num_mpgs;
    cache->mpg_sz = mpg_sz;

    // Allocate memory for entries and hash table. Return NULL if unable.
    cache->entry = FsCalloc(num_mpgs, sizeof(ftlmcEntry));
    if (cache->entry == NULL) {
        FsFree(cache);
        return NULL;
    }
    cache->hash_tbl = FsCalloc(num_mpgs, sizeof(ftlmcEntry*));
    if (cache->hash_tbl == NULL) {
        FsFree(cache->entry);
        FsFree(cache);
        return NULL;
    }

    // Allocate memory to hold MPN contents. Return NULL if unable.
    data_space = FsAalloc(num_mpgs * mpg_sz);
    if (data_space == NULL) {
        FsFree(cache->hash_tbl);
        FsFree(cache->entry);
        FsFree(cache);
        return NULL;
    }

    // Initialize the cache and return cache handle.
    cache->entry[0].data = data_space;
    ftlmcInit(cache);
    return cache;
}

//   ftlmcInit: Initialize the cache
//
//       Input: cache = handle to cache
//
void ftlmcInit(FTLMC* cache) {
    ui32 i, vpns_per_mpg = cache->mpg_sz / sizeof(ui32);
    ui32* memp = cache->entry[0].data;

    // Initialize cache's least recently used list.
    CIRC_LIST_INIT(&cache->lru_list);

    // Loop to initialize each cache entry.
    for (i = 0; i < cache->num_mpgs; ++i, memp += vpns_per_mpg) {
        // Assign pointer to cache entry's page data buffer.
        cache->entry[i].data = memp;

        // Initialize entry as unused and clean.
        cache->entry[i].mpn = (ui32)-1;
        cache->entry[i].status = FTLMC_CLEAN;

        // Append entry to cache's least recently used list.
        CIRC_LIST_APPEND(&cache->entry[i].lru_link, &cache->lru_list);

        // Initialize the entry hash table pointers.
        cache->entry[i].hash_head = NULL;
        cache->hash_tbl[i] = NULL;
    }

    // There are no dirty entries at this point.
    cache->num_dirty = 0;
} //lint !e429

//  ftlmcDelete: Delete an FTL cache for map pages
//
//       Input: cache_ptr = pointer to cache handle
//
void ftlmcDelete(FTLMC** cache_ptr) {
    FTLMC* cache = *cache_ptr;

    // Deallocate all the memory associated with the cache.
    FsAfreeClear(&cache->entry[0].data);
    FsFree(cache->entry);
    FsFree(cache->hash_tbl);
    FsFree(cache);

    // Reset pointer to cache handle.
    *cache_ptr = NULL;
}

// ftlmcGetPage: Retrieve contents of map page via cache
//
//      Inputs: cache = cache handle
//              mpn = map page number
//              new_ptr = new map flag address if read, else NULL
//      Output: *new_ptr = TRUE if new mapping for read
//
//     Returns: Pointer to map page data on success, NULL on error
//
void* ftlmcGetPage(FTLMC* cache, ui32 mpn, int* new_ptr) {
    ftlmcEntry* entry;
    CircLink* link;
    uint hash_ndx;

#if MC_DEBUG
    check_cache(cache, mpn);
#endif

    // Find page's hash table entry.
    entry = cache->hash_tbl[hash(mpn, cache->num_mpgs)];

    // Search hash entry for matching page number.
    for (; entry; entry = entry->next_hash) {
        // Check if page numbers match.
        if (entry->mpn == mpn) {
            // Move entry to end of LRU list.
            CIRC_NODE_REMOVE(&entry->lru_link);
            CIRC_LIST_APPEND(&entry->lru_link, &cache->lru_list);

            // If reading, flag that this page is mapped (because cached).
            if (new_ptr)
                *new_ptr = FALSE;

            // Else writing. If entry was clean, mark it dirty.
            else if (entry->status == FTLMC_CLEAN) {
                ++cache->num_dirty;
                PfAssert(cache->num_dirty <= cache->num_mpgs);
                entry->status = FTLMC_DIRTY;
            }

            // Return pointer to page data.
            return entry->data;
        }
    }

    // Not cached. Search LRU list for least recently used clean entry
    // If none found, use least recently used entry (head of LRU list).
    for (link = CIRC_LIST_HEAD(&cache->lru_list);; link = link->next_bck) {
        // Check if at end of least recently used list.
        if (CIRC_LIST_AT_END(link, &cache->lru_list)) {
            // Re-use cache's least recently used entry (its LRU head).
            link = CIRC_LIST_HEAD(&cache->lru_list);

            // Convert 'lru_link' pointer to entry pointer.
            entry = (void*)((ui8*)link - offsetof(ftlmcEntry, lru_link));

            // Check if page is dirty.
            if (entry->status == FTLMC_DIRTY) {
                // Write old page contents. Return NULL if error.
                if (cache->write_TFS(cache->ftl, entry->mpn, entry->data))
                    return NULL;

                // Mark page clean and decrement dirty count.
                entry->status = FTLMC_CLEAN;
                PfAssert(cache->num_dirty);
                --cache->num_dirty;
            }

            // Break to use this entry.
            break;
        }

        // Convert 'lru_link' pointer to entry pointer.
        entry = (ftlmcEntry*)((ui8*)link - offsetof(ftlmcEntry, lru_link));

        // Break to use this entry if it is clean.
        if (entry->status == FTLMC_CLEAN)
            break;
    }

    // Move entry to end of LRU list.
    CIRC_NODE_REMOVE(&entry->lru_link);
    CIRC_LIST_APPEND(&entry->lru_link, &cache->lru_list);

    // If the entry is in the hash table, remove it from there.
    if (entry->hash_head) {
        // If entry is not first, update previous one, else update head.
        if (entry->prev_hash)
            entry->prev_hash->next_hash = entry->next_hash;
        else
            *(entry->hash_head) = entry->next_hash;

        // If there is a next entry (entry is not end-of-list), update it.
        if (entry->next_hash)
            entry->next_hash->prev_hash = entry->prev_hash;
    }

    // Read new page into the cache. Return NULL if error.
    if (cache->read_TFS(cache->ftl, mpn, entry->data, new_ptr))
        return NULL;

    // Set cache entry with new page info.
    entry->mpn = mpn;

    // Determine location in hash table for page.
    hash_ndx = hash(mpn, cache->num_mpgs);

    // Add new entry into the hash table.
    entry->prev_hash = NULL;
    if (cache->hash_tbl[hash_ndx]) {
        entry->next_hash = cache->hash_tbl[hash_ndx];
        cache->hash_tbl[hash_ndx]->prev_hash = entry;
    } else
        entry->next_hash = NULL;
    cache->hash_tbl[hash_ndx] = entry;
    entry->hash_head = &cache->hash_tbl[hash_ndx];

    // If map page write, mark entry as dirty and adjust dirty count.
    if (new_ptr == NULL) {
        entry->status = FTLMC_DIRTY;
        ++cache->num_dirty;
        PfAssert(cache->num_dirty <= cache->num_mpgs);
    }

    // Return pointer to page data.
    return entry->data;
}

// ftlmcFlushPage: Search cache for dirty page to flush
//
//      Inputs: cache = cache handle
//              mpn = MPN to flush
//
//     Returns: 0 on success, -1 on failure
//
int ftlmcFlushPage(FTLMC* cache, ui32 mpn) {
    ftlmcEntry* entry = &cache->entry[0];

    // Loop over cache entries, looking for page to flush.
    for (; entry < &cache->entry[cache->num_mpgs]; ++entry) {
        // Continue if not a page number match.
        if (entry->mpn != mpn)
            continue;

        // Break if page is clean.
        if (entry->status == FTLMC_CLEAN)
            break;

        // Mark clean, adjust dirty count, save page, and return status.
        entry->status = FTLMC_CLEAN;
        PfAssert(cache->num_dirty);
        --cache->num_dirty;
        return cache->write_TFS(cache->ftl, entry->mpn, entry->data);
    }

    // Return success.
    return 0;
}

// ftlmcFlushMap: Flush all dirty map pages
//
//       Input: cache = cache handle
//
//     Returns: 0 on success, -1 on failure
//
int ftlmcFlushMap(FTLMC* cache) {
    ftlmcEntry* entry = &cache->entry[0];

    // Loop over cache entries, flushing each dirty one.
    for (; entry < &cache->entry[cache->num_mpgs]; ++entry) {
        // Break if all map pages are clean.
        if (cache->num_dirty == 0)
            break;

        // If this page is dirty, save it and mark it clean.
        if (entry->status == FTLMC_DIRTY) {
            entry->status = FTLMC_CLEAN;
            PfAssert(cache->num_dirty);
            --cache->num_dirty;
            if (cache->write_TFS(cache->ftl, entry->mpn, entry->data))
                return -1;
        }
    }

    // No more dirty entries at this point. Return success.
    PfAssert(cache->num_dirty == 0);
    return 0;
}

// ftlmcInCache: Check if map page to be written is in the cache
//
//      Inputs: cache = cache handle
//              mpn = map page number
//
//     Returns: Pointer to page data, NULL if page not in cache
//
ui32* ftlmcInCache(FTLMC* cache, ui32 mpn) {
    ftlmcEntry* entry;

#if MC_DEBUG
    check_cache(cache, mpn);
#endif

    // Find page's hash table entry.
    entry = cache->hash_tbl[hash(mpn, cache->num_mpgs)];

    // Search hash entry for matching page number.
    for (; entry; entry = entry->next_hash) {
        // Check if page numbers match.
        if (entry->mpn == mpn) {
            // If not clean, mark page clean and decrement dirty count.
            if (entry->status != FTLMC_CLEAN) {
                entry->status = FTLMC_CLEAN;
                PfAssert(cache->num_dirty);
                --cache->num_dirty;
            }

            // Return pointer to cached page's data.
            return entry->data;
        }
    }

    // Page is not in cache, return NULL.
    return NULL;
}

