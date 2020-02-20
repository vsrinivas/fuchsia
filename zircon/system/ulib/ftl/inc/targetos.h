// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <stdint.h>

/***********************************************************************/
/* TargetOS Release Number                                             */
/***********************************************************************/
#define TARGETOS_RELEASE 20184

/***********************************************************************/
/* General Definitions                                                 */
/***********************************************************************/
#define i8 char
#define si8 int8_t
#define i16 int16_t
#define i32 int32_t
#define i64 int64_t

#define ui8 uint8_t
#define ui16 uint16_t
#define ui32 unsigned int
#define ui64 uint64_t

#define vi8 volatile int8_t
#define vi16 volatile int16_t
#define vi32 volatile int32_t

#define vui8 volatile uint8_t
#define vui16 volatile uint16_t
#define vui32 volatile uint32_t

// __USE_MISC determines whether these types are defined for linux-arm64.
// Only define these if they're not already defined.
#ifndef __USE_MISC
#define uint unsigned int
#define ulong unsigned long
#endif

#undef TRUE
#define TRUE 1
#undef FALSE
#define FALSE 0
#define ESC 0x1B

/***********************************************************************/
/* Type Definitions                                                    */
/***********************************************************************/

// Circular Linked List Structure
typedef struct circ_list {
  struct circ_list* next_fwd;
  struct circ_list* next_bck;
} CircLink;

// Volatile Circular Linked List Node (used for task and timer lists)
typedef volatile struct vol_circ_list {
  volatile struct vol_circ_list* next_fwd;
  volatile struct vol_circ_list* next_bck;
} vCircLink;

/***********************************************************************/
/* Macro Definitions                                                   */
/***********************************************************************/
#define REG_8(addr) (*(volatile ui8*)(addr))
#define REG16(addr) (*(volatile ui16*)(addr))
#define REG32(addr) (*(volatile ui32*)(addr))
#define REGPTR(addr) (*(void* volatile*)(addr))
#define REGPTRC(addr) (*(const void* volatile*)(addr))
#define REGPTRV(addr) (*(volatile void* volatile*)(addr))

#define MIN(a, b) (((a) < (b)) ? (a) : (b))
#define MAX(a, b) (((a) > (b)) ? (a) : (b))

#define SEQ_GE(a, b) ((i32)((a) - (b)) >= 0)
#define SEQ_GT(a, b) ((i32)((a) - (b)) > 0)
#define SEQ_LE(a, b) ((i32)((a) - (b)) <= 0)
#define SEQ_LT(a, b) ((i32)((a) - (b)) < 0)

#define ALIGN2B(size) (((size) + 1) & ~1)
#define ALIGN4B(size) (((size) + 3) & ~3)
#define ALIGN8B(size) (((size) + 7) & ~7)
#define ALIGN_ADDR(addr, amnt) (void*)((((ui32)(addr) + (amnt)-1) / (amnt)) * amnt)
#define ROUND_UP(size, algn) ((((size) + (algn)-1) / (algn)) * algn)

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

#define WR16_BE(val, addr)                         \
  do {                                             \
    *((ui8*)(addr) + 0) = (ui8)((uint)(val) >> 8); \
    *((ui8*)(addr) + 1) = (ui8)((uint)(val) >> 0); \
  } /*lint -e(717) */                              \
  while (0)

#define WR24_BE(val, addr)                          \
  do {                                              \
    *((ui8*)(addr) + 0) = (ui8)((uint)(val) >> 16); \
    *((ui8*)(addr) + 1) = (ui8)((uint)(val) >> 8);  \
    *((ui8*)(addr) + 2) = (ui8)((uint)(val) >> 0);  \
  } /*lint -e(717) */                               \
  while (0)

#define WR32_BE(val, addr)                          \
  do {                                              \
    *((ui8*)(addr) + 0) = (ui8)((uint)(val) >> 24); \
    *((ui8*)(addr) + 1) = (ui8)((uint)(val) >> 16); \
    *((ui8*)(addr) + 2) = (ui8)((uint)(val) >> 8);  \
    *((ui8*)(addr) + 3) = (ui8)((uint)(val) >> 0);  \
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

#define SWAP32(val)                                                                        \
  ((((val)&0x000000FFU) << 24) | (((val)&0x0000FF00U) << 8) | (((val)&0x00FF0000U) >> 8) | \
   (((val)&0xFF000000U) >> 24))

#define SWAP16(s) (ui16)(((s) << 8) | (ui8)((uint)(s) >> 8))

#define IS_POW2(x) ((x) && (((x) & (~(x) + 1)) == (x)))

// Bit Flags Accessor Macros
#define SET_FLAG(flags, bit_flag) (flags |= (bit_flag))
#define CLR_FLAG(flags, bit_flag) (flags &= ~(bit_flag))
#define FLAG_IS_SET(flags, bit_flag) (flags & (bit_flag))
#define FLAG_IS_CLR(flags, bit_flag) (!(flags & (bit_flag)))

// Circular Linked List Management Macros

#define CIRC_LIST_INIT(lst) ((lst)->next_fwd = (lst)->next_bck = (lst))
#define CIRC_NODE_INIT(node) CIRC_LIST_INIT(node)

#define CIRC_NODE_APPEND(free_node, list_node)     \
  do {                                             \
    (free_node)->next_bck = (list_node)->next_bck; \
    (free_node)->next_fwd = (list_node);           \
    (list_node)->next_bck->next_fwd = (free_node); \
    (list_node)->next_bck = (free_node);           \
  } while (0)

#define CIRC_NODE_INSERT(free_node, list_node)     \
  do {                                             \
    (free_node)->next_fwd = (list_node)->next_fwd; \
    (free_node)->next_bck = (list_node);           \
    (list_node)->next_fwd->next_bck = (free_node); \
    (list_node)->next_fwd = (free_node);           \
  } while (0)

#define CIRC_LIST_APPEND(node, list) CIRC_NODE_INSERT(node, list)
#define CIRC_LIST_INSERT(node, list) CIRC_NODE_APPEND(node, list)
#define CIRC_TO_NODEP(link, offset) (void*)((ui8*)(link)-offset)

#define CIRC_HEAD_REMOVE(link)                     \
  do {                                             \
    (link)->next_bck = (link)->next_bck->next_bck; \
    (link)->next_bck->next_fwd = (link);           \
  } while (0)

#define CIRC_NODE_REMOVE(link)                     \
  do {                                             \
    (link)->next_bck->next_fwd = (link)->next_fwd; \
    (link)->next_fwd->next_bck = (link)->next_bck; \
  } while (0)

#define CIRC_LIST_HEAD(list) ((list)->next_bck)
#define CIRC_LIST_TAIL(list) ((list)->next_fwd)
#define CIRC_LIST_AT_END(link, list) ((link) == (list))
#define CIRC_LIST_EMPTY(list) ((list)->next_bck == (list))

#ifndef EOF_TFS
#define EOF_TFS (-1)
#endif
