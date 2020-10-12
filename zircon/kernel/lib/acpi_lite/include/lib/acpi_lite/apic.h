// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ZIRCON_KERNEL_LIB_ACPI_LITE_INCLUDE_LIB_ACPI_LITE_APIC_H_
#define ZIRCON_KERNEL_LIB_ACPI_LITE_INCLUDE_LIB_ACPI_LITE_APIC_H_

#include <lib/acpi_lite.h>
#include <lib/acpi_lite/structures.h>
#include <zircon/types.h>

namespace acpi_lite {

// Enumerate all enabled Processor Local APICs in the system, calling
// |callback| once for each.
//
// Each entry corresponds to an entry in the ACPI MADT table of type "Local
// Apic". See ACPI v6.3 Section 5.2.12.2 for details.
//
// |callback| is invoked once for each entry.  The function returns any error
// returned by the callback, or if an error was found attempting to parse the
// tables.
zx_status_t EnumerateProcessorLocalApics(
    const AcpiParserInterface& parser,
    const fbl::Function<zx_status_t(const AcpiMadtLocalApicEntry&)>& callback);

// Enumerate all IO APICs in the system, calling |callback| once for each.
//
// |callback| is called onced per IO APIC entry in the ACPI MADT table. The
// function returns any error returned by the callback, or if an error was
// found attempting to parse the tables.
zx_status_t EnumerateIoApics(
    const AcpiParserInterface& parser,
    const fbl::Function<zx_status_t(const AcpiMadtIoApicEntry&)>& callback);

// Enumerate all ISA interrupt source override entries in the system MADT
// table.
//
// By default, it is assumed that the first _n_ APIC interrupts correspond to
// the first _n_ legacy ISA interrupts. Entries in this table record any
// exceptions to this assumption.
//
// |callback| is called once per override entry in the ACPI MADT table. The
// function returns any error returned by the callback, or if an error was
// found attempting to parse the tables.
zx_status_t EnumerateIoApicIsaOverrides(
    const AcpiParserInterface& parser,
    const fbl::Function<zx_status_t(const AcpiMadtIntSourceOverrideEntry&)>& callback);

}  // namespace acpi_lite

#endif  // ZIRCON_KERNEL_LIB_ACPI_LITE_INCLUDE_LIB_ACPI_LITE_APIC_H_
