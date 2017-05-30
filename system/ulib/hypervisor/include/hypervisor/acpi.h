// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <magenta/types.h>

__BEGIN_CDECLS

/**
 * Create an ACPI 1.0 table.
 *
 * @param addr The mapped address of guest physical memory.
 * @param size The size of guest physical memory.
 * @param acpi_off The offset to write the ACPI table.
 */
mx_status_t guest_create_acpi_table(uintptr_t addr, size_t size, uintptr_t acpi_off);

__END_CDECLS
