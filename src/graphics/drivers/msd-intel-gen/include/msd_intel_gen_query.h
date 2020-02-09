// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef MSD_INTEL_GEN_QUERY_H
#define MSD_INTEL_GEN_QUERY_H

#include "magma_common_defs.h"

#define MAGMA_VENDOR_ID_INTEL 0x8086

enum MsdIntelGenQuery {
  kMsdIntelGenQuerySubsliceAndEuTotal = MAGMA_QUERY_VENDOR_PARAM_0,
  kMsdIntelGenQueryGttSize = MAGMA_QUERY_VENDOR_PARAM_0 + 1,
  kMsdIntelGenQueryExtraPageCount = MAGMA_QUERY_VENDOR_PARAM_0 + 2,
};

#endif  // MSD_INTEL_GEN_QUERY_H
