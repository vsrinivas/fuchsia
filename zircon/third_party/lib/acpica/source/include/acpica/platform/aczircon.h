// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#ifndef ZIRCON_THIRD_PARTY_LIB_ACPICA_SOURCE_INCLUDE_ACPICA_PLATFORM_ACZIRCON_H_
#define ZIRCON_THIRD_PARTY_LIB_ACPICA_SOURCE_INCLUDE_ACPICA_PLATFORM_ACZIRCON_H_

#include <stdbool.h>

#include <arch/interrupt.h>
#include <arch/spinlock.h>

/*
 * Settings described in section 7 of
 * https://acpica.org/sites/acpica/files/acpica-reference_17.pdf
 */

#if __x86_64__
#define ACPI_MACHINE_WIDTH 64
#else
#error Unexpected architecture
#endif

#define ACPI_FLUSH_CPU_CACHE() __asm__ volatile("wbinvd")

// Use the standard library headers
#define ACPI_USE_STANDARD_HEADERS
#define ACPI_USE_SYSTEM_CLIBRARY

// Use the builtin cache implementation
#define ACPI_USE_LOCAL_CACHE

// Specify the types Zircon uses for various common objects
#define ACPI_CPU_FLAGS interrupt_saved_state_t
#define ACPI_SPINLOCK arch_spin_lock_t *

// Borrowed from aclinuxex.h

// Include the gcc header since we're compiling on gcc
#include "acgcc.h"

__BEGIN_CDECLS
bool _acpica_acquire_global_lock(void *FacsPtr);
bool _acpica_release_global_lock(void *FacsPtr);
__END_CDECLS

#define ACPI_ACQUIRE_GLOBAL_LOCK(FacsPtr, Acq) Acq = _acpica_acquire_global_lock(FacsPtr)
#define ACPI_RELEASE_GLOBAL_LOCK(FacsPtr, Pnd) Pnd = _acpica_release_global_lock(FacsPtr)

#endif  // ZIRCON_THIRD_PARTY_LIB_ACPICA_SOURCE_INCLUDE_ACPICA_PLATFORM_ACZIRCON_H_
