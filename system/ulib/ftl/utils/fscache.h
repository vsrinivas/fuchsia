// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef _FSCACHE
#define _FSCACHE

#define CLEAN 0
#define DIRTY_NEW 1  // from a new write_TFS
#define DIRTY_OLD 2  // from an overwrite

// Values for the flags field in the Cache structure
#define CACHE_DIRTY (1 << 0)

#define SET_DIRTY_NEW(C, ent) {    \
    if (C) {                       \
        (ent)->state = DIRTY_NEW;  \
        (C)->flags |= CACHE_DIRTY; \
    }                              \
}

#define SET_DIRTY_OLD(C, ent) { \
    (ent)->state = DIRTY_OLD;   \
    (C)->flags |= CACHE_DIRTY;  \
}

typedef struct cache_entry FcEntry;
struct cache_entry {
    FcEntry* next_lru;   // next and prev for the LRU list is
    FcEntry* prev_lru;   // a double linked list
    FcEntry* next_hash;  // next and prev for the hash list is
    FcEntry* prev_hash;  // a double linked list
    FcEntry** hash_head; // pointer to head of hash table list
    ui8* data;           // pointer to cached data
    ui8* dirty_map;      // dirty bitmap of pages in sector
    void* file_ptr;      // pointer to file control information
    ui32 sect_num;       // sector number in actual medium
    ui16 pin_cnt;        // pin counter: 0 = unpinned, > 0 pinned
    ui16 state;          // clean, dirty_new, dirty_old flag
};

typedef int (*MedWFunc)(FcEntry* entry, int update, void* vol_ptr);
typedef int (*MedRFunc)(void* head, ui32 sect_num, void* vol_ptr);

typedef struct {
    FcEntry* pool;       // array containing all cache entries
    FcEntry** hash_tbl;  // hash table to point to pool
    FcEntry* lru_head;   // head of the LRU list
    FcEntry* lru_tail;   // tail of the LRU list
    ui32 pool_size;      // number of cache entries
    ui32 sector_number;  // current sector being worked on
    MedWFunc wr_sect;    // write function to write sector back
    MedWFunc wr_page;    // write function to write page back
    MedRFunc rd_sect;    // read function to read sector
    ui32 block_sects;    // num sects in memory block
    ui32 flags;          // cache flags
    ui32 meta_ents;      // number of non-file data sector entries
    ui32 meta_threshold; // min # of non-file data sector entries
    ui32 sector_pages;   // number of pages in sector
    ui32 tot_access;     // number of get requests
    ui32 hit_access;     // number of hits on get requests
    ui32 ram_used;       // RAM used by cache in bytes
    void* vol_ptr;       // FS volume cache belongs to
} Cache;

int FcInit(Cache* C, ui32 pool_size, MedWFunc wrf, MedRFunc rdf, ui32 sect_sz, ui32 tmp_ents,
           ui32 block_sects, void* volp);
int FcInitMeta(Cache* C, ui32 pool_size, ui32 meta_threshold, MedWFunc wr_sect, MedWFunc wr_page,
               MedRFunc rd_sect, ui32 sect_sz, ui32 pg_sz, ui32 tmp_ents, void* volp);
void FcReinit(Cache* C, ui32 entry_size);
void FcDestroy(Cache* C);
void FcRmvEntry(Cache* C, ui32 entry_number);
FcEntry* FcGetEntry(Cache* C, ui32 ent_number, int skip_rd, void* filep);
void FcFreeEntry(Cache* C, FcEntry** entry);
int FcFlush(Cache* C);
void FcUpdateEntry(Cache* C, FcEntry* entry, ui32 entry_number);
FcEntry* FcInCache(const Cache* C, ui32 entry_number);
void FcSetDirtyNewPgs(Cache* C, FcEntry* ent, ui32 start, ui32 n);
int FcHitsPercent(const Cache* C);
int FcWriteSect(const Cache* C, FcEntry* ent, int update);
int FcHash(uint sector_number, uint size);
void FcRmvFmLRU(Cache* C, FcEntry* entry);
ui32 FcRAM(const Cache* C);
void FcDiag(Cache* C);

#endif // _FSCACHE
