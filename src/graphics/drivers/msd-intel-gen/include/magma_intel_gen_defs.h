// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef MAGMA_INTEL_GEN_DEFS_H
#define MAGMA_INTEL_GEN_DEFS_H

#include "magma_common_defs.h"

#define MAGMA_VENDOR_ID_INTEL 0x8086

enum MagmaIntelGenQuery {
  kMagmaIntelGenQuerySubsliceAndEuTotal = MAGMA_QUERY_VENDOR_PARAM_0,
  kMagmaIntelGenQueryGttSize = MAGMA_QUERY_VENDOR_PARAM_0 + 1,
  kMagmaIntelGenQueryExtraPageCount = MAGMA_QUERY_VENDOR_PARAM_0 + 2,
  kMagmaIntelGenQueryTimestamp = MAGMA_QUERY_VENDOR_PARAM_0 + 3,
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

#endif  // MSD_INTEL_GEN_QUERY_H
