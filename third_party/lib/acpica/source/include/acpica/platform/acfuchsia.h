// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <stdbool.h>
#include <threads.h>

#include <zircon/syscalls.h>
#include <semaphore.h>

/*
 * Settings described in section 7 of
 * https://acpica.org/sites/acpica/files/acpica-reference_17.pdf
 */


#if __x86_64__
#define ACPI_MACHINE_WIDTH 64
#elif __x86__
#define ACPI_MACHINE_WIDTH 32
#define ACPI_USE_NATIVE_DIVIDE
#else
#error Unexpected architecture
#endif

extern zx_handle_t root_resource_handle;

#define ACPI_FLUSH_CPU_CACHE() zx_acpi_cache_flush(root_resource_handle)

// Use the standard library headers
#define ACPI_USE_STANDARD_HEADERS
#define ACPI_USE_SYSTEM_CLIBRARY

// Use the builtin cache implementation
#define ACPI_USE_LOCAL_CACHE

// Specify the types Fuchsia uses for various common objects
#define ACPI_CPU_FLAGS int
#define ACPI_SPINLOCK mtx_t*
#define ACPI_SEMAPHORE sem_t*

// Borrowed from aclinuxex.h

// Include the gcc header since we're compiling on gcc
#include "acgcc.h"

extern bool _acpica_acquire_global_lock(void *FacsPtr);
extern bool _acpica_release_global_lock(void *FacsPtr);
#define ACPI_ACQUIRE_GLOBAL_LOCK(FacsPtr, Acq) Acq = _acpica_acquire_global_lock(FacsPtr)
#define ACPI_RELEASE_GLOBAL_LOCK(FacsPtr, Pnd) Pnd = _acpica_release_global_lock(FacsPtr)
