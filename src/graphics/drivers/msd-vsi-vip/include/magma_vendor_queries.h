// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_GRAPHICS_DRIVERS_MSD_VSI_VIP_INCLUDE_MAGMA_VENDOR_QUERIES_H_
#define SRC_GRAPHICS_DRIVERS_MSD_VSI_VIP_INCLUDE_MAGMA_VENDOR_QUERIES_H_

#include "magma_common_defs.h"

#define MAGMA_VENDOR_ID_VSI 0x10001  // VK_VENDOR_ID_VIV

enum MsdVsiVendorQuery {
  kMsdVsiVendorQueryChipIdentity = MAGMA_QUERY_VENDOR_PARAM_0,
  kMsdVsiVendorQueryChipOption = MAGMA_QUERY_VENDOR_PARAM_0 + 1,
  kMsdVsiVendorQueryClientGpuAddrRange = MAGMA_QUERY_VENDOR_PARAM_0 + 2,
  kMsdVsiVendorQueryExternalSram = MAGMA_QUERY_VENDOR_PARAM_0 + 3,
};

#endif  // SRC_GRAPHICS_DRIVERS_MSD_VSI_VIP_INCLUDE_MAGMA_VENDOR_QUERIES_H_
