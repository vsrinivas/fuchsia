// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#ifndef MAGMA_VENDOR_QUERIES_H_
#define MAGMA_VENDOR_QUERIES_H_

#include "magma_common_defs.h"

enum MsdArmVendorQuery {
    kMsdArmVendorQueryL2Present = MAGMA_QUERY_VENDOR_PARAM_0,
    kMsdArmVendorQueryMaxThreads = MAGMA_QUERY_VENDOR_PARAM_0 + 1,
    kMsdArmVendorQueryThreadMaxBarrierSize = MAGMA_QUERY_VENDOR_PARAM_0 + 2,
    kMsdArmVendorQueryThreadMaxWorkgroupSize = MAGMA_QUERY_VENDOR_PARAM_0 + 3,
    kMsdArmVendorQueryShaderPresent = MAGMA_QUERY_VENDOR_PARAM_0 + 4,
    kMsdArmVendorQueryTilerFeatures = MAGMA_QUERY_VENDOR_PARAM_0 + 5,
    kMsdArmVendorQueryThreadFeatures = MAGMA_QUERY_VENDOR_PARAM_0 + 6,
    kMsdArmVendorQueryL2Features = MAGMA_QUERY_VENDOR_PARAM_0 + 7,
    kMsdArmVendorQueryMemoryFeatures = MAGMA_QUERY_VENDOR_PARAM_0 + 8,
    kMsdArmVendorQueryMmuFeatures = MAGMA_QUERY_VENDOR_PARAM_0 + 9,
};

#endif // MAGMA_VENDOR_QUERIES_H_
