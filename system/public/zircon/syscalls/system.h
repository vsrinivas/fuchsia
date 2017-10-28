// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <zircon/types.h>

__BEGIN_CDECLS

// Commands used by zx_system_powerctl()
#define ZX_SYSTEM_POWERCTL_ENABLE_ALL_CPUS              1u
#define ZX_SYSTEM_POWERCTL_DISABLE_ALL_CPUS_BUT_PRIMARY 2u
#define ZX_SYSTEM_POWERCTL_ACPI_TRANSITION_S_STATE      3u

typedef struct zx_system_powerctl_arg {
    union {
        struct {
            uint8_t target_s_state; // Value between 1 and 5 indicating which S-state
            uint8_t sleep_type_a; // Value from ACPI VM (SLP_TYPa)
            uint8_t sleep_type_b; // Value from ACPI VM (SLP_TYPb)
        } acpi_transition_s_state;
    };
} zx_system_powerctl_arg_t;

__END_CDECLS
