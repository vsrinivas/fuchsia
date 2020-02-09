// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef MSD_QCOM_ADRENO_H
#define MSD_QCOM_ADRENO_H

#include "magma_common_defs.h"

#define MAGMA_VENDOR_ID_QCOM 0x05c6

enum MsdQcomQuery {
  kMsdQcomQueryClientGpuAddrRange = MAGMA_QUERY_VENDOR_PARAM_0,
};

#endif  // MSD_QCOM_ADRENO_H
