// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_BOARD_DRIVERS_MACHINA_MACHINA_H_
#define SRC_DEVICES_BOARD_DRIVERS_MACHINA_MACHINA_H_

#include <fuchsia/hardware/platform/bus/c/banjo.h>
#include <zircon/types.h>

// BTI IDs for our devices
enum {
  BTI_SYSMEM,
};

// Map all of 0-1GB into kernel space in one shot.
#define PERIPHERAL_BASE_PHYS (0)
#define PERIPHERAL_BASE_SIZE (0x40000000UL)  // 1GB

// Individual peripherals in this mapping.
#define PCIE_ECAM_BASE_PHYS ((zx_paddr_t)(PERIPHERAL_BASE_PHYS + 0x808100000))
#define PCIE_ECAM_SIZE (0x100000)
#define PCIE_MMIO_BASE_PHYS ((zx_paddr_t)(PERIPHERAL_BASE_PHYS + 0x808200000))
#define PCIE_MMIO_SIZE (0x100000)
#define PCIE_INT_BASE (32)
#define RTC_BASE_PHYS ((zx_paddr_t)(PERIPHERAL_BASE_PHYS + 0x808301000))
#define RTC_SIZE (0x1000)

typedef struct {
  pbus_protocol_t pbus;
} machina_board_t;

// machina-sysmem.c
zx_status_t machina_sysmem_init(machina_board_t *bus);

#endif  // SRC_DEVICES_BOARD_DRIVERS_MACHINA_MACHINA_H_
