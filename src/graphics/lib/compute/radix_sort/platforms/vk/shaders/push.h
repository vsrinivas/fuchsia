// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_GRAPHICS_LIB_COMPUTE_RADIX_SORT_PLATFORMS_VK_SHADERS_PUSH_H_
#define SRC_GRAPHICS_LIB_COMPUTE_RADIX_SORT_PLATFORMS_VK_SHADERS_PUSH_H_

//
//
//

#define RS_MAX_KEYVALS ((1 << 30) - 1)

//
//
//

#define RS_RADIX_LOG2 8
#define RS_RADIX_SIZE (1 << RS_RADIX_LOG2)

//
// LOOKBACK STATUS FLAGS
//
// The decoupled lookback status flags are stored in the two
// high bits of the count:
//
//   0                                   31
//   | REDUCTION OR PREFIX COUNT | STATUS |
//   +---------------------------+--------+
//   |             30            |    2   |
//
// This limits the keyval extent size to (2^30-1).
//
// Valid status flags are:
//
//   EVEN PASS                 ODD PASS
//   -----------------------   -----------------------
//   0 : invalid               0 : prefix available
//   1 : reduction available   1 : ---
//   2 : prefix available      2 : invalid
//   3 : ---                   3 : reduction available
//
// Atomically adding +1 to a "reduction available" status results in a "prefix
// available" status.
//
// clang-format off
#define RS_PARTITION_STATUS_EVEN_INVALID    (0u)
#define RS_PARTITION_STATUS_EVEN_REDUCTION  (1u)
#define RS_PARTITION_STATUS_EVEN_PREFIX     (2u)

#define RS_PARTITION_STATUS_ODD_INVALID     (2u)
#define RS_PARTITION_STATUS_ODD_REDUCTION   (3u)
#define RS_PARTITION_STATUS_ODD_PREFIX      (0u)
// clang-format on

//
// Define the push constant structures shared by the host and device.
//
//   HISTOGRAM
//   ---------
//   struct rs_push_histogram
//   {
//     uint64_t devaddr_histograms;    // address of histograms extent
//     uint64_t devaddr_keyvals;       // address of keyvals extent
//     uint32_t passes;                // number of passes
//   };
//
//   PREFIX
//   ------
//   struct rs_push_prefix
//   {
//     uint64_t devaddr_histograms;    // address of histograms extent
//   };
//
//   SCATTER
//   -------
//   struct rs_push_scatter
//   {
//     uint64_t devaddr_keyvals_in;    // address of input keyvals
//     uint64_t devaddr_keyvals_out;   // address of output keyvals
//     uint64_t devaddr_partitions     // address of partitions
//     uint64_t devaddr_histogram;     // address of pass histogram
//     uint32_t pass_offset;           // keyval pass offset
//   };
//

#define RS_STRUCT_PUSH_HISTOGRAM()                                                                 \
  struct rs_push_histogram                                                                         \
  {                                                                                                \
    RS_STRUCT_MEMBER(RS_DEVADDR, devaddr_histograms)                                               \
    RS_STRUCT_MEMBER(RS_DEVADDR, devaddr_keyvals)                                                  \
    RS_STRUCT_MEMBER(uint32_t, passes)                                                             \
  }

#define RS_STRUCT_PUSH_PREFIX()                                                                    \
  struct rs_push_prefix                                                                            \
  {                                                                                                \
    RS_STRUCT_MEMBER(RS_DEVADDR, devaddr_histograms)                                               \
  }

#define RS_STRUCT_PUSH_SCATTER()                                                                   \
  struct rs_push_scatter                                                                           \
  {                                                                                                \
    RS_STRUCT_MEMBER(RS_DEVADDR, devaddr_keyvals_even)                                             \
    RS_STRUCT_MEMBER(RS_DEVADDR, devaddr_keyvals_odd)                                              \
    RS_STRUCT_MEMBER(RS_DEVADDR, devaddr_partitions)                                               \
    RS_STRUCT_MEMBER(RS_DEVADDR, devaddr_histograms)                                               \
    RS_STRUCT_MEMBER(uint32_t, pass_offset)                                                        \
  }

////////////////////////////////////////////////////////////////////
//
// GLSL
//
#ifdef VULKAN  // defined by GLSL/VK compiler

// clang-format off
#define RS_STRUCT_MEMBER(type_, name_)         type_ name_;
#define RS_STRUCT_MEMBER_STRUCT(type_, name_)  type_ name_;
// clang-format on

////////////////////////////////////////////////////////////////////
//
// C/C++
//
#else

#ifdef __cplusplus
extern "C" {
#endif

//
//
//

#include <stdint.h>

// clang-format off
#define RS_DEVADDR                            uint64_t
#define RS_STRUCT_MEMBER(type_, name_)        type_ name_;
#define RS_STRUCT_MEMBER_STRUCT(type_, name_) struct type_ name_;
// clang-format on

RS_STRUCT_PUSH_HISTOGRAM();
RS_STRUCT_PUSH_PREFIX();
RS_STRUCT_PUSH_SCATTER();

//
//
//

#ifdef __cplusplus
}
#endif

#endif

//
//
//

#endif  // SRC_GRAPHICS_LIB_COMPUTE_RADIX_SORT_PLATFORMS_VK_SHADERS_PUSH_H_
