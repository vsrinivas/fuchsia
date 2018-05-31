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
#define ZX_SYSTEM_POWERCTL_X86_SET_PKG_PL1              4u
#define ZX_SYSTEM_POWERCTL_REBOOT                       5u
#define ZX_SYSTEM_POWERCTL_REBOOT_BOOTLOADER            6u
#define ZX_SYSTEM_POWERCTL_REBOOT_RECOVERY              7u
#define ZX_SYSTEM_POWERCTL_SHUTDOWN                     8u

typedef struct zx_system_powerctl_arg {
    union {
        struct {
            uint8_t target_s_state; // Value between 1 and 5 indicating which S-state
            uint8_t sleep_type_a; // Value from ACPI VM (SLP_TYPa)
            uint8_t sleep_type_b; // Value from ACPI VM (SLP_TYPb)
        } acpi_transition_s_state;
        struct {
            uint32_t power_limit; // PL1 value in milliwatts
            uint32_t time_window; // PL1 time window in microseconds
            uint8_t clamp;        // PL1 clamping enable
            uint8_t enable;       // PL1 enable
        } x86_power_limit;
    };
} zx_system_powerctl_arg_t;

__END_CDECLS
