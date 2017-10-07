// Copyright 2017 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include "syscalls_system_priv.h"

#include <arch/arch_ops.h>
#include <arch/mp.h>
#include <inttypes.h>
#include <trace.h>

extern "C" {
#include <acpica/acpi.h>
#include <acpica/accommon.h>
#include <acpica/achware.h>
}

#define LOCAL_TRACE 0

zx_status_t arch_system_powerctl(uint32_t cmd, const zx_system_powerctl_arg_t* arg) {
    if (cmd != ZX_SYSTEM_POWERCTL_ACPI_TRANSITION_S_STATE) {
        return ZX_ERR_NOT_SUPPORTED;
    }

    uint8_t target_s_state = arg->acpi_transition_s_state.target_s_state;
    uint8_t sleep_type_a = arg->acpi_transition_s_state.sleep_type_a;
    uint8_t sleep_type_b = arg->acpi_transition_s_state.sleep_type_b;
    if (target_s_state == 0 || target_s_state > 5) {
        TRACEF("Bad S-state: S%u\n", target_s_state);
        return ZX_ERR_INVALID_ARGS;
    }

    // If not a shutdown, ensure CPU 0 is the only cpu left running.
    if (target_s_state != 5 && mp_get_online_mask() != cpu_num_to_mask(0)) {
        TRACEF("Too many CPUs running for state S%u\n", target_s_state);
        return ZX_ERR_BAD_STATE;
    }

    arch_disable_ints();
    // TODO(teisenbe): Set up return vector!
    TRACEF("Entering AcpiHwLegacySleepFinal\n");
    ACPI_STATUS acpi_status = AcpiHwLegacySleepFinal(target_s_state,
                                                     sleep_type_a, sleep_type_b);
    arch_enable_ints();
    if (acpi_status != AE_OK) {
        TRACEF("AcpiHwLegacySleepFinal failed: %x\n", acpi_status);
        return ZX_ERR_INTERNAL;
    }
    return ZX_OK;
}
