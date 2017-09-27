// Copyright 2017 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#pragma once

#include <arch/arm64/el2_state.h>
#include <fbl/ref_ptr.h>
#include <fbl/unique_ptr.h>
#include <hypervisor/packet_mux.h>
#include <zircon/types.h>

class GuestPhysicalAddressSpace;
class PortDispatcher;
class VmObject;

typedef struct zx_port_packet zx_port_packet_t;

class Guest {
public:
    static zx_status_t Create(fbl::RefPtr<VmObject> physmem, fbl::unique_ptr<Guest>* out);
    ~Guest();
    DISALLOW_COPY_ASSIGN_AND_MOVE(Guest);

    zx_status_t SetTrap(uint32_t kind, zx_vaddr_t addr, size_t len,
                        fbl::RefPtr<PortDispatcher> port, uint64_t key);
    zx_status_t NextVpid(uint8_t* vpid);

    GuestPhysicalAddressSpace* AddressSpace() const { return gpas_.get(); }

private:
    fbl::unique_ptr<GuestPhysicalAddressSpace> gpas_;
    PacketMux mux_;
    uint8_t vmid_ = 0;
    uint8_t next_vpid_ = 0;

    Guest() = default;
};

class Vcpu {
public:
    static zx_status_t Create(zx_vaddr_t ip, uint8_t vpid, GuestPhysicalAddressSpace* gpas,
                              fbl::unique_ptr<Vcpu>* out);
    DISALLOW_COPY_ASSIGN_AND_MOVE(Vcpu);

    zx_status_t Resume(zx_port_packet_t* packet);

private:
    const thread_t* thread_;
    El2State el2_state_;

    Vcpu(const thread_t* thread);
};

/* Create a guest. */
zx_status_t arch_guest_create(fbl::RefPtr<VmObject> physmem, fbl::unique_ptr<Guest>* guest);

/* Set a trap within a guest. */
zx_status_t arch_guest_set_trap(Guest* guest, uint32_t kind, zx_vaddr_t addr, size_t len,
                                fbl::RefPtr<PortDispatcher> port, uint64_t key);

/* Create a VCPU. */
zx_status_t arm_vcpu_create(zx_vaddr_t ip, uint8_t vpid, GuestPhysicalAddressSpace* gpas,
                            fbl::unique_ptr<Vcpu>* out);

/* Resume execution of a VCPU. */
zx_status_t arch_vcpu_resume(Vcpu* vcpu, zx_port_packet_t* packet);

/* Issue an interrupt on a VCPU. */
zx_status_t arch_vcpu_interrupt(Vcpu* vcpu, uint32_t interrupt);

/* Read the register state of a VCPU. */
zx_status_t arch_vcpu_read_state(const Vcpu* vcpu, uint32_t kind, void* buffer, uint32_t len);

/* Write the register state of a VCPU. */
zx_status_t arch_vcpu_write_state(Vcpu* vcpu, uint32_t kind, const void* buffer, uint32_t len);
