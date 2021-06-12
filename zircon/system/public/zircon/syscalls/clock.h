// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SYSROOT_ZIRCON_SYSCALLS_CLOCK_H_
#define SYSROOT_ZIRCON_SYSCALLS_CLOCK_H_

#include <zircon/time.h>

// clang-format off

// Argument version identifiers.
//
// All zx_clock_* syscalls which fetch or receive a structure's worth of
// arguments encode a version number in the options field of the syscall.  This
// version field is in the same location and is the same size for each syscall,
// so a common set of macros may be used for encoding and decoding.
#define ZX_CLOCK_ARGS_VERSION_SHIFT ((uint64_t)58u)
#define ZX_CLOCK_ARGS_VERSION_BITS  ((uint64_t)6u)
#define ZX_CLOCK_ARGS_VERSION_MASK \
  (((((uint64_t)1) << ZX_CLOCK_ARGS_VERSION_BITS) - 1) << ZX_CLOCK_ARGS_VERSION_SHIFT)
#define ZX_CLOCK_ARGS_VERSION(_N) \
  (((uint64_t)(_N) << ZX_CLOCK_ARGS_VERSION_SHIFT) & ZX_CLOCK_ARGS_VERSION_MASK)

// Clock creation options.
#define ZX_CLOCK_OPT_MONOTONIC  ((uint64_t)1u << 0)
#define ZX_CLOCK_OPT_CONTINUOUS ((uint64_t)1u << 1)
#define ZX_CLOCK_OPT_AUTO_START ((uint64_t)1u << 2)

#define ZX_CLOCK_OPTS_ALL ( \
        ZX_CLOCK_OPT_MONOTONIC | \
        ZX_CLOCK_OPT_CONTINUOUS | \
        ZX_CLOCK_OPT_AUTO_START)

// v1 clock update flags
#define ZX_CLOCK_UPDATE_OPTION_VALUE_VALID        ((uint64_t)1u << 0)
#define ZX_CLOCK_UPDATE_OPTION_RATE_ADJUST_VALID  ((uint64_t)1u << 1)
#define ZX_CLOCK_UPDATE_OPTION_ERROR_BOUND_VALID  ((uint64_t)1u << 2)

// Additional v2 clock update flags
#define ZX_CLOCK_UPDATE_OPTION_REFERENCE_VALUE_VALID  ((uint64_t)1u << 3)
#define ZX_CLOCK_UPDATE_OPTION_SYNTHETIC_VALUE_VALID  ZX_CLOCK_UPDATE_OPTION_VALUE_VALID
#define ZX_CLOCK_UPDATE_OPTION_BOTH_VALUES_VALID ( \
        ZX_CLOCK_UPDATE_OPTION_REFERENCE_VALUE_VALID | \
        ZX_CLOCK_UPDATE_OPTION_SYNTHETIC_VALUE_VALID)

#define ZX_CLOCK_UPDATE_OPTIONS_ALL ( \
        ZX_CLOCK_UPDATE_OPTION_SYNTHETIC_VALUE_VALID |  \
        ZX_CLOCK_UPDATE_OPTION_RATE_ADJUST_VALID | \
        ZX_CLOCK_UPDATE_OPTION_ERROR_BOUND_VALID | \
        ZX_CLOCK_UPDATE_OPTION_REFERENCE_VALUE_VALID)

// Clock rate adjustment limits
#define ZX_CLOCK_UPDATE_MIN_RATE_ADJUST ((int32_t)-1000)
#define ZX_CLOCK_UPDATE_MAX_RATE_ADJUST ((int32_t)1000)

// Special clock error values
#define ZX_CLOCK_UNKNOWN_ERROR  ((uint64_t)0xFFFFFFFFFFFFFFFF)

// clang-format on

// Make certain that the new name introduced with the v2 version of the clock
// update structure always tracks the original v2 name.
#ifdef __cplusplus
static_assert(ZX_CLOCK_UPDATE_OPTION_VALUE_VALID == ZX_CLOCK_UPDATE_OPTION_SYNTHETIC_VALUE_VALID,
              "Mismatch between v1 VALUE and v2 SYNTHETIC_VALUE clock update flags!");
#else
_Static_assert(ZX_CLOCK_UPDATE_OPTION_VALUE_VALID == ZX_CLOCK_UPDATE_OPTION_SYNTHETIC_VALUE_VALID,
               "Mismatch between v1 VALUE and v2 SYNTHETIC_VALUE clock update flags!");
#endif

typedef struct zx_clock_create_args_v1 {
  zx_time_t backstop_time;
} zx_clock_create_args_v1_t;

typedef struct zx_clock_rate {
  uint32_t synthetic_ticks;
  uint32_t reference_ticks;
} zx_clock_rate_t;

typedef struct zx_clock_transformation {
  int64_t reference_offset;
  int64_t synthetic_offset;
  zx_clock_rate_t rate;
} zx_clock_transformation_t;

typedef struct zx_clock_details_v1 {
  uint64_t options;
  zx_time_t backstop_time;
  zx_clock_transformation_t ticks_to_synthetic;
  zx_clock_transformation_t mono_to_synthetic;
  uint64_t error_bound;
  zx_ticks_t query_ticks;
  zx_ticks_t last_value_update_ticks;
  zx_ticks_t last_rate_adjust_update_ticks;
  zx_ticks_t last_error_bounds_update_ticks;
  uint32_t generation_counter;
  uint8_t padding1[4];
} zx_clock_details_v1_t;

typedef struct zx_clock_update_args_v1 {
  int32_t rate_adjust;
  uint8_t padding1[4];
  int64_t value;
  uint64_t error_bound;
} zx_clock_update_args_v1_t;

typedef struct zx_clock_update_args_v2 {
  int32_t rate_adjust;
  uint8_t padding1[4];
  int64_t synthetic_value;
  int64_t reference_value;
  uint64_t error_bound;
} zx_clock_update_args_v2_t;

#endif  // SYSROOT_ZIRCON_SYSCALLS_CLOCK_H_
