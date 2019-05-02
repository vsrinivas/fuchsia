// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

//
// TODO:
//
// Add Key-Val sorting support -- easy.
//

#include <stdio.h>
#include <stdint.h>

//
// All code generation is driven by the specified architectural
// details and host platform API.
//
// In general, the warps-per-block and keys-per-thread are the
// critical knobs for tuning performance.
//

#define HSG_CONFIG_DEFINE_LEN_SIZE 64

struct hsg_config
{
  struct {
    char        lower[HSG_CONFIG_DEFINE_LEN_SIZE];  // symbolic name -- lower-cased
    char        upper[HSG_CONFIG_DEFINE_LEN_SIZE];  // symbolic name -- upper-cased
  } define;

  struct {
    struct {
      uint32_t  warps;                              // the number of warps in the flip merge kernel -- may become obsolete
      uint32_t  lo;                                 // lo scale factor for flip merge
      uint32_t  hi;                                 // hi scale factor for flip merge
    } flip;

    struct {
      uint32_t  warps;                              // the number of warps in the half merge kernel -- may become obsolete
      uint32_t  lo;                                 // lo scale factor for half merge
      uint32_t  hi;                                 // hi scale factor for half merge
    } half;
  } merge;

  struct {
    uint32_t    warps_min;                          // min warps for a block that uses smem barriers
    uint32_t    warps_max;                          // max warps for the entire multiprocessor
    uint32_t    warps_mod;                          // the number of warps necessary to load balance horizontal merging

    uint32_t    smem_min;                           // minimum amount of shared memory that can be allocated by an arch
    uint32_t    smem_quantum;                       // smem quantum amount for an arch

    uint32_t    smem_bs;                            // amount of shared memory available to block sorting kernel
    uint32_t    smem_bc;                            // usually the same as .smem_bs but can be overridden
  } block;

  struct {
    uint32_t    lanes;                              // number of lanes in arch's warp/wave/subgroup
    uint32_t    lanes_log2;                         // log2 of .lanes
    uint32_t    skpw_bs;                            // another potential clamp on the amount of shared memory
  } warp;

  struct {
    uint32_t    regs;                               // number of in-register values per warp lane
    uint32_t    xtra;                               // explicit "extra" number of registers available for merging
  } thread;

  struct {
    uint32_t    dwords;                             // number of dwords in a key -- .type will be extended to support key-vals
  } type;
};

//
// HotSort can merge non-power-of-two blocks of warps
//

struct hsg_level
{
  uint32_t    count; // networks >= 2

  uint32_t    diffs        [2];
  uint32_t    diff_masks   [2];
  uint32_t    evenodds     [2];
  uint32_t    evenodd_masks[2];
  uint32_t    networks     [2];

  union {
    uint64_t  b64;
    uint32_t  b32a2[2];
  } active;
};

//
//
//

#define MERGE_LEVELS_MAX_LOG2  7 // merge up to 128 warps
#define MERGE_LEVELS_MAX_SIZE  (1 << MERGE_LEVELS_MAX_LOG2)

//
// This is computed
//

struct hsg_merge
{
  uint32_t         offsets [MERGE_LEVELS_MAX_SIZE];
  uint32_t         networks[MERGE_LEVELS_MAX_SIZE];

  struct hsg_level levels[MERGE_LEVELS_MAX_LOG2];

  uint32_t         index;

  uint32_t         warps;

  uint32_t         rows_bs;
  uint32_t         rows_bc;

  uint32_t         skpw_bc;
};

//
//
//

#define HSG_OP_EXPAND_ALL()                                     \
  HSG_OP_EXPAND_X(HSG_OP_TYPE_EXIT)                             \
                                                                \
  HSG_OP_EXPAND_X(HSG_OP_TYPE_END)                              \
  HSG_OP_EXPAND_X(HSG_OP_TYPE_BEGIN)                            \
  HSG_OP_EXPAND_X(HSG_OP_TYPE_ELSE)                             \
                                                                \
  HSG_OP_EXPAND_X(HSG_OP_TYPE_TARGET_BEGIN)                     \
  HSG_OP_EXPAND_X(HSG_OP_TYPE_TARGET_END)                       \
                                                                \
  HSG_OP_EXPAND_X(HSG_OP_TYPE_FILL_IN_KERNEL_PROTO)             \
  HSG_OP_EXPAND_X(HSG_OP_TYPE_FILL_IN_KERNEL_BODY)              \
                                                                \
  HSG_OP_EXPAND_X(HSG_OP_TYPE_FILL_OUT_KERNEL_PROTO)            \
  HSG_OP_EXPAND_X(HSG_OP_TYPE_FILL_OUT_KERNEL_BODY)             \
                                                                \
  HSG_OP_EXPAND_X(HSG_OP_TYPE_TRANSPOSE_KERNEL_PROTO)           \
  HSG_OP_EXPAND_X(HSG_OP_TYPE_TRANSPOSE_KERNEL_PREAMBLE)        \
  HSG_OP_EXPAND_X(HSG_OP_TYPE_TRANSPOSE_KERNEL_BODY)            \
                                                                \
  HSG_OP_EXPAND_X(HSG_OP_TYPE_BS_KERNEL_PROTO)                  \
  HSG_OP_EXPAND_X(HSG_OP_TYPE_BS_KERNEL_PREAMBLE)               \
                                                                \
  HSG_OP_EXPAND_X(HSG_OP_TYPE_BC_KERNEL_PROTO)                  \
  HSG_OP_EXPAND_X(HSG_OP_TYPE_BC_KERNEL_PREAMBLE)               \
                                                                \
  HSG_OP_EXPAND_X(HSG_OP_TYPE_FM_KERNEL_PROTO)                  \
  HSG_OP_EXPAND_X(HSG_OP_TYPE_FM_KERNEL_PREAMBLE)               \
                                                                \
  HSG_OP_EXPAND_X(HSG_OP_TYPE_HM_KERNEL_PROTO)                  \
  HSG_OP_EXPAND_X(HSG_OP_TYPE_HM_KERNEL_PREAMBLE)               \
                                                                \
  HSG_OP_EXPAND_X(HSG_OP_TYPE_BX_REG_GLOBAL_LOAD)               \
  HSG_OP_EXPAND_X(HSG_OP_TYPE_BX_REG_GLOBAL_STORE)              \
                                                                \
  HSG_OP_EXPAND_X(HSG_OP_TYPE_FM_REG_GLOBAL_LOAD_LEFT)          \
  HSG_OP_EXPAND_X(HSG_OP_TYPE_FM_REG_GLOBAL_STORE_LEFT)         \
  HSG_OP_EXPAND_X(HSG_OP_TYPE_FM_REG_GLOBAL_LOAD_RIGHT)         \
  HSG_OP_EXPAND_X(HSG_OP_TYPE_FM_REG_GLOBAL_STORE_RIGHT)        \
  HSG_OP_EXPAND_X(HSG_OP_TYPE_FM_MERGE_RIGHT_PRED)              \
                                                                \
  HSG_OP_EXPAND_X(HSG_OP_TYPE_HM_REG_GLOBAL_LOAD)               \
  HSG_OP_EXPAND_X(HSG_OP_TYPE_HM_REG_GLOBAL_STORE)              \
                                                                \
  HSG_OP_EXPAND_X(HSG_OP_TYPE_SLAB_FLIP)                        \
  HSG_OP_EXPAND_X(HSG_OP_TYPE_SLAB_HALF)                        \
                                                                \
  HSG_OP_EXPAND_X(HSG_OP_TYPE_CMP_FLIP)                         \
  HSG_OP_EXPAND_X(HSG_OP_TYPE_CMP_HALF)                         \
                                                                \
  HSG_OP_EXPAND_X(HSG_OP_TYPE_CMP_XCHG)                         \
                                                                \
  HSG_OP_EXPAND_X(HSG_OP_TYPE_BS_REG_SHARED_STORE_V)            \
  HSG_OP_EXPAND_X(HSG_OP_TYPE_BS_REG_SHARED_LOAD_V)             \
  HSG_OP_EXPAND_X(HSG_OP_TYPE_BC_REG_SHARED_LOAD_V)             \
                                                                \
  HSG_OP_EXPAND_X(HSG_OP_TYPE_BX_REG_SHARED_STORE_LEFT)         \
  HSG_OP_EXPAND_X(HSG_OP_TYPE_BS_REG_SHARED_STORE_RIGHT)        \
                                                                \
  HSG_OP_EXPAND_X(HSG_OP_TYPE_BS_REG_SHARED_LOAD_LEFT)          \
  HSG_OP_EXPAND_X(HSG_OP_TYPE_BS_REG_SHARED_LOAD_RIGHT)         \
                                                                \
  HSG_OP_EXPAND_X(HSG_OP_TYPE_BC_REG_GLOBAL_LOAD_LEFT)          \
                                                                \
  HSG_OP_EXPAND_X(HSG_OP_TYPE_BLOCK_SYNC)                       \
                                                                \
  HSG_OP_EXPAND_X(HSG_OP_TYPE_BS_FRAC_PRED)                     \
                                                                \
  HSG_OP_EXPAND_X(HSG_OP_TYPE_BS_MERGE_H_PREAMBLE)              \
  HSG_OP_EXPAND_X(HSG_OP_TYPE_BC_MERGE_H_PREAMBLE)              \
                                                                \
  HSG_OP_EXPAND_X(HSG_OP_TYPE_BX_MERGE_H_PRED)                  \
                                                                \
  HSG_OP_EXPAND_X(HSG_OP_TYPE_BS_ACTIVE_PRED)                   \
                                                                \
  HSG_OP_EXPAND_X(HSG_OP_TYPE_COUNT)

//
//
//

#undef  HSG_OP_EXPAND_X
#define HSG_OP_EXPAND_X(t) t ,

typedef enum hsg_op_type {

  HSG_OP_EXPAND_ALL()

} hsg_op_type;

//
//
//

struct hsg_op
{
  hsg_op_type   type;

  union {

    uint32_t    params[3];

    struct {
      uint32_t  a;
      uint32_t  b;
      uint32_t  c;
    };

    struct {
      uint32_t  n;
      uint32_t  v;
    };

    struct {
      uint32_t  m;
      uint32_t  w;
    };

  };
};

//
//
//

extern char const * const hsg_op_type_string[];

//
//
//

struct hsg_target
{
  struct hsg_target_state * state;
};

//
// All targets share this prototype
//

typedef
void
(*hsg_target_pfn)(struct hsg_target       * const target,
                  struct hsg_config const * const config,
                  struct hsg_merge  const * const merge,
                  struct hsg_op     const * const ops,
                  uint32_t                  const depth);
//
//
//

extern
void
hsg_target_debug(struct hsg_target       * const target,
                 struct hsg_config const * const config,
                 struct hsg_merge  const * const merge,
                 struct hsg_op     const * const ops,
                 uint32_t                  const depth);

extern
void
hsg_target_cuda(struct hsg_target       * const target,
                struct hsg_config const * const config,
                struct hsg_merge  const * const merge,
                struct hsg_op     const * const ops,
                uint32_t                  const depth);

extern
void
hsg_target_opencl(struct hsg_target       * const target,
                  struct hsg_config const * const config,
                  struct hsg_merge  const * const merge,
                  struct hsg_op     const * const ops,
                  uint32_t                  const depth);

extern
void
hsg_target_glsl(struct hsg_target       * const target,
                struct hsg_config const * const config,
                struct hsg_merge  const * const merge,
                struct hsg_op     const * const ops,
                uint32_t                  const depth);
//
//
//
