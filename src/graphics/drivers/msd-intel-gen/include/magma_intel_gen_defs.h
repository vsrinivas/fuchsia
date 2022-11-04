// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef MAGMA_INTEL_GEN_DEFS_H
#define MAGMA_INTEL_GEN_DEFS_H

#include "magma/magma_common_defs.h"

#define MAGMA_VENDOR_ID_INTEL 0x8086

enum MagmaIntelGenQuery {
  // Returns chip details (simple result)
  kMagmaIntelGenQuerySubsliceAndEuTotal = MAGMA_QUERY_VENDOR_PARAM_0,
  // Returns the GTT size (simple result)
  kMagmaIntelGenQueryGttSize = MAGMA_QUERY_VENDOR_PARAM_0 + 1,
  // Returns the number of pages of padding used when assigning GPU addresses (simple result)
  kMagmaIntelGenQueryExtraPageCount = MAGMA_QUERY_VENDOR_PARAM_0 + 2,
  // Returns magma_intel_gen_timestamp_query (buffer result)
  kMagmaIntelGenQueryTimestamp = MAGMA_QUERY_VENDOR_PARAM_0 + 3,
  // Returns magma_intel_gen_topology (buffer result, see struct magma_intel_gen_topology)
  kMagmaIntelGenQueryTopology = MAGMA_QUERY_VENDOR_PARAM_0 + 4,
  // Returns boolean (simple result)
  kMagmaIntelGenQueryHasContextIsolation = MAGMA_QUERY_VENDOR_PARAM_0 + 5,
  // Returns timestamp frequency (simple result)
  kMagmaIntelGenQueryTimestampFrequency = MAGMA_QUERY_VENDOR_PARAM_0 + 6,
};

struct magma_intel_gen_timestamp_query {
  uint64_t monotonic_raw_timestamp[2];  // start and end of sample interval
  uint64_t monotonic_timestamp;
  uint64_t device_timestamp;
} __attribute__((packed));

enum MagmaIntelGenCommandBufferFlags {
  kMagmaIntelGenCommandBufferForRender = MAGMA_COMMAND_BUFFER_VENDOR_FLAGS_0,
  kMagmaIntelGenCommandBufferForVideo = MAGMA_COMMAND_BUFFER_VENDOR_FLAGS_0 << 1,
};

struct magma_intel_gen_topology {
  // The number of slices, subslices, and EUs, if none are disabled by masks.
  uint32_t max_slice_count;
  uint32_t max_subslice_count;
  uint32_t max_eu_count;     // executable units
  uint32_t data_byte_count;  // the number of data bytes immediately following this structure

  // A variable amount of mask data follows this structure, starting with a slice enable mask,
  // then for each enabled slice, there follows: a subslice enable mask and an EU enable mask for
  // each enabled subslice. Each mask is contained within a multiple of 8 bits (little endian).
  // Example: 2 slices, 3 subslices, 5 EUs
  // 8 bits (2/2 slices enabled) = 0x3
  // 8 bits (slice 0, 2/3 subslices enabled) = 0x6
  // 8 bits (slice 0 subslice 1, 5/5 EUs enabled) = 0x1F
  // 8 bits (slice 0 subslice 2, 4/5 EUs enabled) = 0x1D
  // 8 bits (slice 1, 1/3 subslices enabled) = 0x2
  // 8 bits (slice 1 subslice 1, 3/5 EUs enabled) = 0x1C

} __attribute__((packed));

#endif  // MSD_INTEL_GEN_QUERY_H
