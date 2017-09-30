// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <fbl/intrusive_single_list.h>
#include <fbl/unique_ptr.h>
#include <hypervisor/io.h>
#include <hypervisor/phys_mem.h>
#include <zircon/types.h>
#include <zx/port.h>

class PciBus;
class IoApic;
class IoPort;

/**
 * Create an identity-mapped page table.
 *
 * @param addr The mapped address of guest physical memory.
 * @param size The size of guest physical memory.
 * @param end_off The offset to the end of the page table.
 */
zx_status_t guest_create_page_table(uintptr_t addr, size_t size, uintptr_t* end_off);

/* Convert a key from a port packet into a pointer to the mapping object. */
static inline IoMapping& trap_key_to_mapping(uint64_t trap_key) {
    return *reinterpret_cast<IoMapping*>(trap_key);
}

enum class TrapType {
    MMIO_SYNC = 0,
    MMIO_BELL = 1,
    PIO_SYNC = 2,
    PIO_ASYNC = 3,
};

class Guest {
public:
    ~Guest();

    zx_status_t Init(size_t mem_size);

    zx_status_t CreatePageTable(uintptr_t* end_off) {
        return guest_create_page_table(phys_mem_.addr(), phys_mem_.size(), end_off);
    }

    const PhysMem& phys_mem() const { return phys_mem_; }
    zx_handle_t handle() const { return guest_; }

    // Setup a trap to delegate accesses to an IO region to |handler|.
    zx_status_t CreateMapping(TrapType type, uint64_t addr, size_t size, uint64_t offset,
                              IoHandler* handler);

    // TODO: Replace these with an interface that allows devices to map
    // callbacks to address ranges.
    PciBus* pci_bus;
    IoApic* io_apic;
    IoPort* io_port;

private:
    zx_status_t IoThread();

    zx_handle_t guest_ = ZX_HANDLE_INVALID;
    PhysMem phys_mem_;

    zx::port port_;
    fbl::SinglyLinkedList<fbl::unique_ptr<IoMapping>> mappings_;
};

typedef struct e820entry {
    uint64_t addr;
    uint64_t size;
    uint32_t type;
} __PACKED e820entry_t;

/**
 * Return the size in bytes of e820 memory map.
 *
 * @param size The size of guest physical memory.
 */
size_t guest_e820_size(size_t size);

/**
 * Create an e820 memory map.
 *
 * @param addr The mapped address of guest physical memory.
 * @param size The size of guest physical memory.
 * @param e820_off The offset to the e820 memory map.
 */
zx_status_t guest_create_e820(uintptr_t addr, size_t size, uintptr_t e820_off);
