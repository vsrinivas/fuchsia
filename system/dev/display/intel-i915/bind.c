// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <ddk/binding.h>
#include <ddk/driver.h>

#include "intel-i915.h"

#define INTEL_I915_VID (0x8086)

static zx_driver_ops_t intel_i915_driver_ops = {
    .version = DRIVER_OPS_VERSION,
    .bind = intel_i915_bind,
};

// clang-format off
ZIRCON_DRIVER_BEGIN(intel_i915, intel_i915_driver_ops, "zircon", "0.1", 27)
    BI_ABORT_IF(NE, BIND_PROTOCOL, ZX_PROTOCOL_PCI),
    BI_ABORT_IF(NE, BIND_PCI_VID, INTEL_I915_VID),
    // Skylake DIDs
    BI_MATCH_IF(EQ, BIND_PCI_DID, 0x191b),
    BI_MATCH_IF(EQ, BIND_PCI_DID, 0x1912),
    BI_MATCH_IF(EQ, BIND_PCI_DID, 0x191d),
    BI_MATCH_IF(EQ, BIND_PCI_DID, 0x1902),
    BI_MATCH_IF(EQ, BIND_PCI_DID, 0x1916),
    BI_MATCH_IF(EQ, BIND_PCI_DID, 0x191e),
    BI_MATCH_IF(EQ, BIND_PCI_DID, 0x1906),
    BI_MATCH_IF(EQ, BIND_PCI_DID, 0x190b),
    BI_MATCH_IF(EQ, BIND_PCI_DID, 0x1926),
    BI_MATCH_IF(EQ, BIND_PCI_DID, 0x1927),
    BI_MATCH_IF(EQ, BIND_PCI_DID, 0x1923),
    BI_MATCH_IF(EQ, BIND_PCI_DID, 0x193b),
    BI_MATCH_IF(EQ, BIND_PCI_DID, 0x192d),
    BI_MATCH_IF(EQ, BIND_PCI_DID, 0x193d),
    // Kaby lake DIDs
    BI_MATCH_IF(EQ, BIND_PCI_DID, 0x5916),
    BI_MATCH_IF(EQ, BIND_PCI_DID, 0x591e),
    BI_MATCH_IF(EQ, BIND_PCI_DID, 0x591b),
    BI_MATCH_IF(EQ, BIND_PCI_DID, 0x5912),
    BI_MATCH_IF(EQ, BIND_PCI_DID, 0x5926),
    BI_MATCH_IF(EQ, BIND_PCI_DID, 0x5906),
    BI_MATCH_IF(EQ, BIND_PCI_DID, 0x5927),
    BI_MATCH_IF(EQ, BIND_PCI_DID, 0x5902),
    BI_MATCH_IF(EQ, BIND_PCI_DID, 0x591a),
    BI_MATCH_IF(EQ, BIND_PCI_DID, 0x591d),
ZIRCON_DRIVER_END(intel_i915)
