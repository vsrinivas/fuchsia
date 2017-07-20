// Copyright 2017 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#pragma once

#include <magenta/syscalls/hypervisor.h>
#include <mxtl/ref_ptr.h>
#include <mxtl/unique_ptr.h>

class FifoDispatcher;
class GuestPhysicalAddressSpace;
class PacketMux;
class VmObject;

class Guest {
public:
    GuestPhysicalAddressSpace* AddressSpace() const { return nullptr; }
    const PacketMux* Mux() const { return nullptr; }
};

class Vcpu {};

/* Create a guest. */
status_t arch_guest_create(mxtl::RefPtr<VmObject> physmem, mxtl::unique_ptr<Guest>* guest);

/* Set a trap within a guest. */
status_t arch_guest_set_trap(Guest* guest, mx_trap_address_space_t aspace, mx_vaddr_t addr,
                             size_t len, mxtl::RefPtr<FifoDispatcher> fifo);

/* Resume execution of a VCPU. */
status_t arch_vcpu_resume(Vcpu* vcpu, mx_guest_packet_t* packet);

/* Issue an interrupt on a VCPU. */
status_t arch_vcpu_interrupt(Vcpu* vcpu, uint32_t interrupt);

/* Read the register state of a VCPU. */
status_t arch_vcpu_read_state(const Vcpu* vcpu, mx_vcpu_state_t* state);

/* Write the register state of a VCPU. */
status_t arch_vcpu_write_state(Vcpu* vcpu, const mx_vcpu_state_t& state);
