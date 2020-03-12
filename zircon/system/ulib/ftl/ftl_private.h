// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <stddef.h>         // For size_t definition.
#include <stdint.h>
#include <zircon/assert.h>
#include <zircon/compiler.h>

#include "ftl.h"
#include "utils/kernel.h"   // For SEM definition.

//
// Configuration.
//
#define FS_ASSERT TRUE        // TRUE enables filesys PfAssert()

//
// Symbol Definitions.
//
#define ui8 uint8_t
#define ui16 uint16_t
#define ui32 unsigned int

// __USE_MISC determines whether these types are defined for linux-arm64.
// Only define these if they're not already defined.
#ifndef __USE_MISC
#define uint unsigned int
#endif

// CRC32 Related Definitions
#define CRC32_START 0xFFFFFFFF // starting CRC bit string
#define CRC32_FINAL 0xDEBB20E3 // summed over data and CRC
#define CRC32_UPDATE(crc, c) ((crc >> 8) ^ Crc32Tbl[(ui8)(crc ^ c)])

//
// Macro Definitions.
//
#define MIN(a, b) (((a) < (b)) ? (a) : (b))

// Bit Flags Accessor Macros
#define FLAG_IS_SET(flags, bit_flag) (flags & (bit_flag))
#define FLAG_IS_CLR(flags, bit_flag) (!(flags & (bit_flag)))

#define WR16_LE(val, addr)                         \
  do {                                             \
    *((ui8*)(addr) + 0) = (ui8)((uint)(val) >> 0); \
    *((ui8*)(addr) + 1) = (ui8)((uint)(val) >> 8); \
  } /*lint -e(717) */                              \
  while (0)

#define WR24_LE(val, addr)                          \
  do {                                              \
    *((ui8*)(addr) + 0) = (ui8)((uint)(val) >> 0);  \
    *((ui8*)(addr) + 1) = (ui8)((uint)(val) >> 8);  \
    *((ui8*)(addr) + 2) = (ui8)((uint)(val) >> 16); \
  } /*lint -e(717) */                               \
  while (0)

#define WR32_LE(val, addr)                          \
  do {                                              \
    *((ui8*)(addr) + 0) = (ui8)((uint)(val) >> 0);  \
    *((ui8*)(addr) + 1) = (ui8)((uint)(val) >> 8);  \
    *((ui8*)(addr) + 2) = (ui8)((uint)(val) >> 16); \
    *((ui8*)(addr) + 3) = (ui8)((uint)(val) >> 24); \
  } /*lint -e(717) */                               \
  while (0)


#define RD16_LE(addr)  (ui16)(((ui16)((ui8*)(addr))[0] <<  0) | \
                              ((ui16)((ui8*)(addr))[1] <<  8))

#define RD24_LE(addr)  (ui32)(((ui32)((ui8*)(addr))[0] <<  0) | \
                              ((ui32)((ui8*)(addr))[1] <<  8) | \
                              ((ui32)((ui8*)(addr))[2] << 16))

#define RD32_LE(addr)  (ui32)(((ui32)((ui8*)(addr))[0] <<  0) | \
                              ((ui32)((ui8*)(addr))[1] <<  8) | \
                              ((ui32)((ui8*)(addr))[2] << 16) | \
                              ((ui32)((ui8*)(addr))[3] << 24))

// Circular Linked List Management Macros
#define CIRC_LIST_INIT(lst) ((lst)->next_fwd = (lst)->next_bck = (lst))
#define CIRC_NODE_INIT(node) CIRC_LIST_INIT(node)
#define CIRC_LIST_HEAD(list) ((list)->next_bck)
#define CIRC_LIST_AT_END(link, list) ((link) == (list))

#define CIRC_LIST_INSERT(free_node, list_node)         \
    do {                                               \
        (free_node)->next_bck = (list_node)->next_bck; \
        (free_node)->next_fwd = (list_node);           \
        (list_node)->next_bck->next_fwd = (free_node); \
        (list_node)->next_bck = (free_node);           \
    } while (0)

#define CIRC_LIST_APPEND(free_node, list_node)         \
    do {                                               \
        (free_node)->next_fwd = (list_node)->next_fwd; \
        (free_node)->next_bck = (list_node);           \
        (list_node)->next_fwd->next_bck = (free_node); \
        (list_node)->next_fwd = (free_node);           \
    } while (0)

#define CIRC_NODE_REMOVE(link)                         \
    do {                                               \
        (link)->next_bck->next_fwd = (link)->next_fwd; \
        (link)->next_fwd->next_bck = (link)->next_bck; \
    } while (0)

#if FS_ASSERT
#define PF_DEBUG
#define PfAssert(c)  ZX_DEBUG_ASSERT(c)
#else
#define PfAssert(c)  do { } while (0)
#endif

//
// Type Declarations.
//

// Circular Linked List Structure
typedef struct circ_list {
    struct circ_list* next_fwd;
    struct circ_list* next_bck;
} CircLink;

__BEGIN_CDECLS
//
// Variable Declarations.
//
extern SEM FileSysSem;
extern const uint32_t Crc32Tbl[256];

//
// Function Prototypes.
//
int FsError(int fs_err_code);
int FsError2(int fs_err_code, int errno_code);

// TargetFTL-TargetNDM Interface
int ndmEraseBlock(uint32_t pn, NDM ndm);
int ndmReadPages(uint32_t pn0, uint32_t count, uint8_t* buf, uint8_t* spare, NDM ndm);
int ndmReadSpare(uint32_t vpn, uint8_t* spare, NDM ndm);
int ndmWritePages(uint32_t pn0, uint32_t cnt, const uint8_t* buf, uint8_t* spare, NDM ndm);
int ndmWritePage(uint32_t vpn, const uint8_t* buf, uint8_t* spare, NDM ndm);
int ndmTransferPage(uint32_t old_vpn, uint32_t new_vpn, uint8_t* buf, uint8_t* spare, NDM ndm);
int ndmCheckPage(uint32_t pn, uint8_t* data, uint8_t* spare, NDM ndm);
uint32_t ndmPairOffset(uint32_t page_offset, CNDM ndm);
uint32_t ndmPastPrevPair(CNDM ndm, uint32_t pn);
int FtlNdmDelVol(const char* name);
void* FtlnAddVol(FtlNdmVol* ftl_cfg, XfsVol* ftl_inf);

void free_clear(void* alloc_ptr_addr);

// Cache Line-Aligned Allocation/Deallocation Routines
void* aalloc(size_t size);
void afree_clear(void* aaloc_ptr_addr);

// FTL Memory Allocation Wrapper Functions
void* FsCalloc(size_t nmemb, size_t size);
void* FsMalloc(size_t size);
void* FsAalloc(size_t size);
void FsFreeClear(void* ptr_ptr);
void FsAfreeClear(void* ptr_ptr);
void FsFree(void* ptr);

__END_CDECLS

