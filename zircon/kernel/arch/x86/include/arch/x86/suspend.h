// Copyright 2021 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#ifndef ZIRCON_KERNEL_ARCH_X86_INCLUDE_ARCH_X86_SUSPEND_H_
#define ZIRCON_KERNEL_ARCH_X86_INCLUDE_ARCH_X86_SUSPEND_H_

#include <stdint.h>
#include <zircon/types.h>

// Enter a system sleep state via the FADT PM registers.
//
// This function must be called with interrupts disabled.
//
// This function puts the system in a sleep state using the ACPI FADT PM registers. It is intended
// to replace the second half of the ACPICA function AcpiHwLegacySleep which must be called with
// interrupts disabled, however in Fuchsia ACPICA runs in usermode and there is no mechanism for it
// to disable interrupts. Instead it calls on the kernel which first disables interrupts before
// calling on this function to transition the system to the sleep state. The function returns ZX_OK
// when the system wakes.
extern "C" zx_status_t set_suspend_registers(uint8_t sleep_state, uint8_t sleep_type_a,
                                             uint8_t sleep_type_b);

#endif  // ZIRCON_KERNEL_ARCH_X86_INCLUDE_ARCH_X86_SUSPEND_H_
