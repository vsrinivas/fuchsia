// Copyright 2021 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#ifndef ZIRCON_KERNEL_ARCH_X86_INCLUDE_ARCH_X86_ACPI_H_
#define ZIRCON_KERNEL_ARCH_X86_INCLUDE_ARCH_X86_ACPI_H_

#include <stdint.h>

// Initiates a transition to the requested ACPI S-state.
//
// This must not be called before bootstrap16 is configured to handle the resume.
//
// This must be called from a kernel thread, unless it is a transition to
// S5 (poweroff).  Failure to do so may result in loss of usermode register state.
extern "C" int32_t x86_acpi_transition_s_state(struct x86_realmode_entry_data_registers* regs,
                                               uint8_t target_s_state, uint8_t sleep_type_a,
                                               uint8_t sleep_type_b);

#endif  // ZIRCON_KERNEL_ARCH_X86_INCLUDE_ARCH_X86_ACPI_H_
