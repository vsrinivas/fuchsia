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

/* TPM memory range. */
#define TPM_PHYS_BASE                   0xfed40000
#define TPM_PHYS_TOP                    TPM_PHYS_BASE + 0x5000 - 1

/* UART ports. */
#define UART_BASE                       0x3f8
#define UART_SIZE                       0x8

/* Use an async trap for the first port (TX port) only. */
#define UART_ASYNC_BASE                 UART_BASE
#define UART_ASYNC_SIZE                 1
#define UART_ASYNC_OFFSET               0

/* Use an async trap for the first port (TX port) only. */
#define UART_SYNC_BASE                  (UART_BASE + UART_ASYNC_SIZE)
#define UART_SYNC_SIZE                  (UART_SIZE - UART_ASYNC_SIZE)
#define UART_SYNC_OFFSET                UART_ASYNC_SIZE

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
#define PIT_CHANNEL_0                   0x40
#define PIT_CONTROL_PORT                0x43

/* PCI config ports. */
#define PCI_CONFIG_ADDRESS_PORT_BASE    0xcf8
#define PCI_CONFIG_ADDRESS_PORT_TOP     (PCI_CONFIG_ADDRESS_PORT_BASE + 3)
#define PCI_CONFIG_DATA_PORT_BASE       0xcfc
#define PCI_CONFIG_DATA_PORT_TOP        (PCI_CONFIG_DATA_PORT_BASE + 3)

// clang-format on
