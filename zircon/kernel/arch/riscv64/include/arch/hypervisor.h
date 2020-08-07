// Copyright 2020 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#ifndef ZIRCON_KERNEL_ARCH_RISCV64_INCLUDE_ARCH_HYPERVISOR_H_
#define ZIRCON_KERNEL_ARCH_RISCV64_INCLUDE_ARCH_HYPERVISOR_H_

#include <zircon/syscalls/hypervisor.h>
#include <zircon/types.h>

#include <fbl/ref_ptr.h>
#include <hypervisor/guest_physical_address_space.h>
#include <hypervisor/id_allocator.h>
#include <hypervisor/interrupt_tracker.h>
#include <hypervisor/page.h>
#include <hypervisor/trap_map.h>
#include <kernel/event.h>
#include <kernel/spinlock.h>
#include <ktl/unique_ptr.h>

typedef struct zx_port_packet zx_port_packet_t;
class PortDispatcher;
enum class InterruptState : uint8_t;

class Guest {
 public:
  static zx_status_t Create(ktl::unique_ptr<Guest>* out);
  ~Guest();
  DISALLOW_COPY_ASSIGN_AND_MOVE(Guest);

  zx_status_t SetTrap(uint32_t kind, zx_vaddr_t addr, size_t len, fbl::RefPtr<PortDispatcher> port,
                      uint64_t key);

  hypervisor::GuestPhysicalAddressSpace* AddressSpace() const { return gpas_.get(); }
  hypervisor::TrapMap* Traps() { return &traps_; }
  uint8_t Vmid() const { return vmid_; }

  zx_status_t AllocVpid(uint8_t* vpid);
  zx_status_t FreeVpid(uint8_t vpid);

 private:
  ktl::unique_ptr<hypervisor::GuestPhysicalAddressSpace> gpas_;
  hypervisor::TrapMap traps_;
  const uint8_t vmid_;

  DECLARE_MUTEX(Guest) vcpu_mutex_;
  // TODO(alexlegg): Find a good place for this constant to live (max vcpus).
  hypervisor::IdAllocator<uint8_t, 8> TA_GUARDED(vcpu_mutex_) vpid_allocator_;

  explicit Guest(uint8_t vmid);
};


class Vcpu {
 public:
  static zx_status_t Create(Guest* guest, zx_vaddr_t entry, ktl::unique_ptr<Vcpu>* out);
  ~Vcpu();
  DISALLOW_COPY_ASSIGN_AND_MOVE(Vcpu);

  zx_status_t Resume(zx_port_packet_t* packet);
  void Interrupt(uint32_t vector, hypervisor::InterruptType type);
  zx_status_t ReadState(zx_vcpu_state_t* state) const;
  zx_status_t WriteState(const zx_vcpu_state_t& state);
  zx_status_t WriteState(const zx_vcpu_io_t& io_state) { return ZX_ERR_INVALID_ARGS; }

 private:
  Vcpu(Guest* guest, uint8_t vpid, const Thread* thread);
};

#endif  // ZIRCON_KERNEL_ARCH_RISCV64_INCLUDE_ARCH_HYPERVISOR_H_
