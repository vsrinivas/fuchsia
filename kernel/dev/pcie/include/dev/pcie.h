// Copyright 2016 The Fuchsia Authors
// Copyright (c) 2016, Google, Inc. All rights reserved
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT


#pragma once

#include <magenta/compiler.h>
#include <dev/pci.h>
#include <dev/pcie_bridge.h>
#include <dev/pcie_bus_driver.h>
#include <dev/pcie_device.h>
#include <endian.h>
#include <kernel/mutex.h>
#include <mxtl/ref_ptr.h>
#include <sys/types.h>

struct pcie_config_t {
    pci_config_t base;
    uint8_t      __pad0[PCIE_BASE_CONFIG_SIZE - sizeof(pci_config_t)];
    uint8_t      extended[PCIE_EXTENDED_CONFIG_SIZE - PCIE_BASE_CONFIG_SIZE];
} __PACKED;

void pcie_scan_function(const mxtl::RefPtr<pcie_bridge_state_t>& upstream_bridge,
                        pcie_config_t*                           cfg,
                        uint                                     dev_id,
                        uint                                     func_id);

/*
 * Endian independent PCIe register access helpers.
 */
static inline uint8_t  pcie_read8 (const volatile uint8_t*  reg) { return *reg; }
static inline uint16_t pcie_read16(const volatile uint16_t* reg) { return LE16(*reg); }
static inline uint32_t pcie_read32(const volatile uint32_t* reg) { return LE32(*reg); }

static inline void pcie_write8 (volatile uint8_t*  reg, uint8_t  val) { *reg = val; }
static inline void pcie_write16(volatile uint16_t* reg, uint16_t val) { *reg = LE16(val); }
static inline void pcie_write32(volatile uint32_t* reg, uint32_t val) { *reg = LE32(val); }

inline mxtl::RefPtr<pcie_bridge_state_t> pcie_device_state_t::DowncastToBridge() {
    return is_bridge ? mxtl::WrapRefPtr(static_cast<pcie_bridge_state_t*>(this)) : nullptr;
}

static inline mxtl::RefPtr<pcie_device_state_t>
pcie_upcast_to_device(const mxtl::RefPtr<pcie_bridge_state_t>& bridge) {
    return mxtl::WrapRefPtr(static_cast<pcie_device_state_t*>(bridge.get()));
}

static inline mxtl::RefPtr<pcie_device_state_t>
pcie_upcast_to_device(mxtl::RefPtr<pcie_bridge_state_t>&& bridge) {
    return mxtl::internal::MakeRefPtrNoAdopt(static_cast<pcie_device_state_t*>(bridge.leak_ref()));
}

/**
 * Fetches a ref'ed pointer to the Nth PCIe device currently in the system.
 * Used for iterating through all PCIe devices.
 *
 * @param index The 0-based index of the device to fetch.
 *
 * @return A ref'ed pointer the requested device, or NULL if no such device
 * exists.
 */
mxtl::RefPtr<pcie_device_state_t> pcie_get_nth_device(uint32_t index);
