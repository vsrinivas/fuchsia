// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stdint.h>

#include <zircon/compiler.h>
#include <zircon/features.h>
#include <zircon/syscalls.h>
#include <zircon/types.h>

#include "private.h"

zx_status_t _zx_system_get_features(uint32_t kind, uint32_t* features) {
    switch (kind) {
    case ZX_FEATURE_KIND_CPU: {
        uint32_t cpu_features = DATA_CONSTANTS.features.cpu;
        if (!(cpu_features & ZX_HAS_CPU_FEATURES)) {
            return ZX_ERR_NOT_SUPPORTED;
        }

        *features = cpu_features;
        return ZX_OK;
    }
    default:
        return ZX_ERR_NOT_SUPPORTED;
    }
}

VDSO_INTERFACE_FUNCTION(zx_system_get_features);
