// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <fbl/function.h>
#include <fbl/intrusive_single_list.h>
#include <fbl/unique_ptr.h>
#include <hypervisor/io.h>
#include <hypervisor/phys_mem.h>
#include <hypervisor/vcpu.h>
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
    using VcpuFactory = fbl::Function<zx_status_t(Guest* guest, uintptr_t entry, uint64_t id,
                                                  Vcpu* vcpu)>;

    ~Guest();

    zx_status_t Init(size_t mem_size);

    const PhysMem& phys_mem() const { return phys_mem_; }
    zx_handle_t handle() const { return guest_; }

    // Setup a trap to delegate accesses to an IO region to |handler|.
    zx_status_t CreateMapping(TrapType type, uint64_t addr, size_t size, uint64_t offset,
                              IoHandler* handler);

    // Setup a handler function to run when an additional VCPU is brought up. The factory should
    // call Start on the new VCPU to begin executing the guest on a new thread.
    void RegisterVcpuFactory(VcpuFactory factory);

    // Initializes a VCPU by calling the VCPU factory. The first VCPU must have id 0.
    zx_status_t StartVcpu(uintptr_t entry, uint64_t id);

    // Waits for all VCPUs associated with the guest to finish executing.
    zx_status_t Join();

private:
    // TODO(alexlegg): Consolidate this constant with other definitions in Garnet.
    static constexpr size_t kMaxVcpus = 16u;

    fbl::Mutex mutex_;

    zx_status_t IoThread();

    zx_handle_t guest_ = ZX_HANDLE_INVALID;
    PhysMem phys_mem_;

    zx::port port_;
    fbl::SinglyLinkedList<fbl::unique_ptr<IoMapping>> mappings_;

    VcpuFactory vcpu_factory_ = [](Guest* guest, uintptr_t entry, uint64_t id, Vcpu* vcpu) {
        return ZX_ERR_BAD_STATE;
    };
    fbl::unique_ptr<Vcpu> vcpus_[kMaxVcpus] = {};
};
