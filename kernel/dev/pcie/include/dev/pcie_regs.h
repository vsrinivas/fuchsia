// Copyright 2016 The Fuchsia Authors
// Copyright (c) 2016, Google, Inc. All rights reserved
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#pragma once

#include <magenta/compiler.h>
#include <dev/pci.h>
#include <dev/pcie_constants.h>
#include <endian.h>
#include <sys/types.h>

struct pcie_config_t {
    pci_config_t base;
    uint8_t      __pad0[PCIE_BASE_CONFIG_SIZE - sizeof(pci_config_t)];
    uint8_t      extended[PCIE_EXTENDED_CONFIG_SIZE - PCIE_BASE_CONFIG_SIZE];
} __PACKED;

/*
 * Endian independent PCIe register access helpers.
 */
static inline uint8_t  pcie_read8 (const volatile uint8_t*  reg) { return *reg; }
static inline uint16_t pcie_read16(const volatile uint16_t* reg) { return LE16(*reg); }
static inline uint32_t pcie_read32(const volatile uint32_t* reg) { return LE32(*reg); }

static inline void pcie_write8 (volatile uint8_t*  reg, uint8_t  val) { *reg = val; }
static inline void pcie_write16(volatile uint16_t* reg, uint16_t val) { *reg = LE16(val); }
static inline void pcie_write32(volatile uint32_t* reg, uint32_t val) { *reg = LE32(val); }
