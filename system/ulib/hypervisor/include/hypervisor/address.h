// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <limits.h>

/* The size of an ECAM region depends on values in the MCFG ACPI table. For
 * each ECAM region there is a defined physical base address as well as a bus
 * start/end value for that region.
 *
 * When creating an ECAM address for a PCI configuration register, the bus
 * value must be relative to the starting bus number for that ECAM region.
 */
#define PCI_ECAM_SIZE(start_bus, end_bus) \
    (((end_bus) - (start_bus)) << 20)

// clang-format off

/* Local APIC memory range. */
#define LOCAL_APIC_PHYS_BASE            0xfee00000
#define LOCAL_APIC_PHYS_TOP             (LOCAL_APIC_PHYS_BASE + PAGE_SIZE - 1)

/* IO APIC memory range. */
#define IO_APIC_PHYS_BASE               0xfec00000
#define IO_APIC_PHYS_TOP                (IO_APIC_PHYS_BASE + PAGE_SIZE - 1)

/* PCI ECAM memory range. */
#define PCI_ECAM_PHYS_BASE              0xd0000000
#define PCI_ECAM_PHYS_TOP               (PCI_ECAM_PHYS_BASE + PCI_ECAM_SIZE(0, 1) - 1)

/* UART ports. */
#define UART_RECEIVE_PORT               0x3f8
#define UART_TRANSMIT_PORT              0x3f8
#define UART_INTERRUPT_ENABLE_PORT      0x3f9
#define UART_INTERRUPT_ID_PORT          0x3fa
#define UART_LINE_CONTROL_PORT          0x3fb
#define UART_MODEM_CONTROL_PORT         0x3fc
#define UART_LINE_STATUS_PORT           0x3fd
#define UART_MODEM_STATUS_PORT          0x3fe
#define UART_SCR_SCRATCH_PORT           0x3ff

/* RTC ports. */
#define RTC_INDEX_PORT                  0x70
#define RTC_DATA_PORT                   0x71

/* I8042 ports. */
#define I8042_DATA_PORT                 0x60
#define I8042_COMMAND_PORT              0x64

/* PM1 ports. */
#define PM1_EVENT_PORT                  0x1000
#define PM1_CONTROL_PORT                0x2000

/* PIC ports. */
#define PIC1_COMMAND_PORT               0x20
#define PIC1_DATA_PORT                  0x21
#define PIC2_COMMAND_PORT               0xa0
#define PIC2_DATA_PORT                  0xa1

/* PIT ports. */
#define I8253_CHANNEL_0                 0x40
#define I8253_CONTROL_PORT              0x43

/* PCI config ports. */
#define PCI_CONFIG_ADDRESS_PORT_BASE    0xcf8
#define PCI_CONFIG_ADDRESS_PORT_TOP     (PCI_CONFIG_ADDRESS_PORT_BASE + 3)
#define PCI_CONFIG_DATA_PORT_BASE       0xcfc
#define PCI_CONFIG_DATA_PORT_TOP        (PCI_CONFIG_DATA_PORT_BASE + 3)

// clang-format on
