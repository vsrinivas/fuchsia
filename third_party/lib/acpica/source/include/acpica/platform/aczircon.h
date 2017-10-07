// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#pragma once

#include <stdbool.h>

#include <kernel/spinlock.h>

/*
 * Settings described in section 7 of
 * https://acpica.org/sites/acpica/files/acpica-reference_17.pdf
 */

#if __x86_64__
#define ACPI_MACHINE_WIDTH 64
#else
#error Unexpected architecture
#endif

#define ACPI_FLUSH_CPU_CACHE() __asm__ volatile ("wbinvd")

// Use the standard library headers
#define ACPI_USE_STANDARD_HEADERS
#define ACPI_USE_SYSTEM_CLIBRARY

// Use the builtin cache implementation
#define ACPI_USE_LOCAL_CACHE

// Specify the types Zircon uses for various common objects
#define ACPI_CPU_FLAGS spin_lock_saved_state_t
#define ACPI_SPINLOCK spin_lock_t*

// Borrowed from aclinuxex.h

// Include the gcc header since we're compiling on gcc
#include "acgcc.h"

extern bool _acpica_acquire_global_lock(void *FacsPtr);
extern bool _acpica_release_global_lock(void *FacsPtr);
#define ACPI_ACQUIRE_GLOBAL_LOCK(FacsPtr, Acq) Acq = _acpica_acquire_global_lock(FacsPtr)
#define ACPI_RELEASE_GLOBAL_LOCK(FacsPtr, Pnd) Pnd = _acpica_release_global_lock(FacsPtr)
