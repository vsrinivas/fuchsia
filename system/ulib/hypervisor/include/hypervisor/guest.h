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

/* Convert a key from a port packet into a pointer to the mapping object. */
static inline IoMapping* trap_key_to_mapping(uint64_t trap_key) {
    return reinterpret_cast<IoMapping*>(trap_key);
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

    const PhysMem& phys_mem() const { return phys_mem_; }
    zx_handle_t handle() const { return guest_; }

    // Setup a trap to delegate accesses to an IO region to |handler|.
    zx_status_t CreateMapping(TrapType type, uint64_t addr, size_t size, uint64_t offset,
                              IoHandler* handler);

private:
    zx_status_t IoThread();

    zx_handle_t guest_ = ZX_HANDLE_INVALID;
    PhysMem phys_mem_;

    zx::port port_;
    fbl::SinglyLinkedList<fbl::unique_ptr<IoMapping>> mappings_;
};
