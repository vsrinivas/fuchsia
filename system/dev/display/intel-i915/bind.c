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
ZIRCON_DRIVER_BEGIN(intel_i915, intel_i915_driver_ops, "zircon", "*0.1", 3)
    BI_ABORT_IF(NE, BIND_PROTOCOL, ZX_PROTOCOL_PCI),
    BI_ABORT_IF(NE, BIND_PCI_VID, INTEL_I915_VID),
    BI_MATCH_IF(EQ, BIND_PCI_CLASS, 0x3), // Display class
ZIRCON_DRIVER_END(intel_i915)
