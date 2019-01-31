// Copyright 2017 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#pragma once

#include <acpica/acpi.h>
#include <arch/x86/bootstrap16.h>
#include <stdint.h>

__BEGIN_CDECLS

// Initiates a transition to the requested ACPI S-state.
//
// This must not be called before bootstrap16 is configured to handle the resume.
//
// This must be called from a kernel thread, unless it is a transition to
// S5 (poweroff).  Failure to do so may result in loss of usermode register state.
ACPI_STATUS x86_acpi_transition_s_state(struct x86_realmode_entry_data_registers* regs,
                                        uint8_t target_s_state,
                                        uint8_t sleep_type_a, uint8_t sleep_type_b);

__END_CDECLS
