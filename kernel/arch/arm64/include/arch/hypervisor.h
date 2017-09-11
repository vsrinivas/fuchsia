// Copyright 2017 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#pragma once

#include <magenta/types.h>
#include <fbl/ref_ptr.h>
#include <fbl/unique_ptr.h>

class GuestPhysicalAddressSpace;
class PacketMux;
class PortDispatcher;
class VmObject;

typedef struct mx_port_packet mx_port_packet_t;

class Guest {
public:
    static status_t Create(fbl::RefPtr<VmObject> physmem, fbl::unique_ptr<Guest>* out);
    ~Guest();
    DISALLOW_COPY_ASSIGN_AND_MOVE(Guest);

    GuestPhysicalAddressSpace* AddressSpace() const { return nullptr; }
    const PacketMux* Mux() const { return nullptr; }

private:
    const uint8_t vmid_;

    explicit Guest(uint8_t vmid);
};

class Vcpu {};

/* Create a guest. */
mx_status_t arch_guest_create(fbl::RefPtr<VmObject> physmem, fbl::unique_ptr<Guest>* guest);

/* Set a trap within a guest. */
mx_status_t arch_guest_set_trap(Guest* guest, uint32_t kind, mx_vaddr_t addr, size_t len,
                                fbl::RefPtr<PortDispatcher> port, uint64_t key);

/* Resume execution of a VCPU. */
mx_status_t arch_vcpu_resume(Vcpu* vcpu, mx_port_packet_t* packet);

/* Issue an interrupt on a VCPU. */
mx_status_t arch_vcpu_interrupt(Vcpu* vcpu, uint32_t interrupt);

/* Read the register state of a VCPU. */
mx_status_t arch_vcpu_read_state(const Vcpu* vcpu, uint32_t kind, void* buffer, uint32_t len);

/* Write the register state of a VCPU. */
mx_status_t arch_vcpu_write_state(Vcpu* vcpu, uint32_t kind, const void* buffer, uint32_t len);
