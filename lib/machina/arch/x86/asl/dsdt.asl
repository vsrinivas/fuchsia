// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// From ACPI 6.1, Section 19.6.28: For compatibility with ACPI versions before
// ACPI 2.0, the bit width of Integer objects is dependent on the
// ComplianceRevision of the DSDT. If the ComplianceRevision is less than 2, all
// integers are restricted to 32 bits. Otherwise, full 64-bit integers are used.
// The version of the DSDT sets the global integer width for all integers,
// including integers in SSDTs.
DefinitionBlock ("dsdt.aml", "DSDT", 2, "ZX", "ZX DSDT", 0x0)
{
    Name (PICM, Zero)
    Method (_PIC, 1, NotSerialized)                                 // _PIC: Interrupt Model
    {
        PICM = Arg0
    }

    // System \_S5 State (Soft Off).
    Name (_S5, Package () { 0x01, 0x00, 0x00 })

    Scope (_SB)
    {
        Device (PCI0)
        {
            Name (_HID, EisaId ("PNP0A08") /* PCI Express Bus */)   // _HID: Hardware ID
            Name (_CID, EisaId ("PNP0A03") /* PCI Bus */)           // _CID: Compatible ID
            Name (_UID, 0)                                          // _UID: Unique ID
            Name (_BBN, 0)                                          // _BBN: Base Bus Number

            Name(_PRT, Package()                                    // _PRT: PCI Routing Table
            {
                // Device 0-11.
                Package() { 0x0000ffff, 0, Zero, 32 },
                Package() { 0x0001ffff, 0, Zero, 33 },
                Package() { 0x0002ffff, 0, Zero, 34 },
                Package() { 0x0003ffff, 0, Zero, 35 },
                Package() { 0x0004ffff, 0, Zero, 36 },
                Package() { 0x0005ffff, 0, Zero, 37 },
                Package() { 0x0006ffff, 0, Zero, 38 },
                Package() { 0x0007ffff, 0, Zero, 39 },
                Package() { 0x0008ffff, 0, Zero, 40 },
                Package() { 0x0009ffff, 0, Zero, 41 },
                Package() { 0x000affff, 0, Zero, 42 },
                Package() { 0x000bffff, 0, Zero, 43 },
                Package() { 0x000cffff, 0, Zero, 44 },
                Package() { 0x000dffff, 0, Zero, 45 },
                Package() { 0x000effff, 0, Zero, 46 },
                Package() { 0x000fffff, 0, Zero, 47 },
            })

            NAME(_CRS, ResourceTemplate() {                         // _CRS: Current Resource Setting
                // Allocate PCI Bus Numbers.
                WordBusNumber(                                      // Produce Bus 0-ff
                    ResourceProducer,
                    MinFixed,
                    MaxFixed,
                    PosDecode,
                    0x0000,                                         // AddressGranularity
                    0x0000,                                         // AddressMin
                    0x00ff,                                         // AddressMax
                    0x0000,                                         // AddressTranslation
                    0x0100                                          // RangeLength
                )

                // Consume PCI Address/Data ports.
                IO(                                                 // Consumed resource (CF8-CFF)
                    Decode16,
                    0x0cf8,                                         // AddressMin
                    0x0cf8,                                         // AddressMax
                    0x0001,                                         // AddressAlignment
                    0x0008                                          // RangeLength
                )

                // Allocated port ranges that this bridge can map devices to.
                // This is used for mapping PIO BARs.
                WordIO(                                             // Produce resource (8000-8FFF)
                    ResourceProducer,
                    MinFixed,
                    MaxFixed,
                    PosDecode,
                    EntireRange,
                    0x0000,                                         // AddressGranularity
                    0x8000,                                         // AddressMin
                    0x8fff,                                         // AddressMax
                    0x0000,                                         // AddressTranslation
                    0x1000                                          // RangeLength
                )

                // Allocated memory ranges that this bridge can map devices to.
                // This is used for mapping MMIO BARs.
                QWordMemory(
                    ResourceProducer,                               // Produce resource (0x00000000f0000000 - 0x00000000ffffffff)
                    PosDecode,
                    MinFixed,
                    MaxFixed,
                    Cacheable,
                    ReadWrite,
                    0x0000000000000000,                             // AddressGranularity
                    0x00000000f0000000,                             // AddressMin
                    0x00000000ffffffff,                             // AddressMax
                    0x0000000000000000,                             // AddressTranslation
                    0x0000000010000000                              // Range Length
                )
            })
        }
    }
}
