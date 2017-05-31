// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// From ACPI 6.1, Section 19.6.28: For compatibility with ACPI versions before
// ACPI 2.0, the bit width of Integer objects is dependent on the
// ComplianceRevision of the DSDT. If the ComplianceRevision is less than 2, all
// integers are restricted to 32 bits. Otherwise, full 64-bit integers are used.
// The version of the DSDT sets the global integer width for all integers,
// including integers in SSDTs.
DefinitionBlock ("dsdt.aml", "DSDT", 2, "MX", "MX DSDT", 0x0)
{
    Name (PICM, Zero)
    Method (_PIC, 1, NotSerialized)  // _PIC: Interrupt Model
    {
        PICM = Arg0
    }
}
