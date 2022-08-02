// Copyright 2017 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#ifndef ZIRCON_KERNEL_ARCH_X86_INCLUDE_ARCH_HYPERVISOR_H_
#define ZIRCON_KERNEL_ARCH_X86_INCLUDE_ARCH_HYPERVISOR_H_

#include <zircon/compiler.h>
#include <zircon/syscalls/hypervisor.h>
#include <zircon/types.h>

#include <arch/x86/apic.h>
#include <arch/x86/hypervisor/vmx_state.h>
#include <arch/x86/interrupts.h>
#include <fbl/ref_ptr.h>
#include <hypervisor/guest_physical_address_space.h>
#include <hypervisor/id_allocator.h>
#include <hypervisor/interrupt_tracker.h>
#include <hypervisor/page.h>
#include <hypervisor/trap_map.h>
#include <kernel/event.h>
#include <kernel/spinlock.h>
#include <kernel/timer.h>
#include <ktl/unique_ptr.h>

class AutoVmcs;
struct VmxInfo;

// Maximum VCPUs per guest.
constexpr size_t kMaxGuestVcpus = 64;

class VmxPage : public hypervisor::Page {
 public:
  zx_status_t Alloc(const VmxInfo& info, uint8_t fill);

 private:
  using hypervisor::Page::Alloc;
};

// Represents a guest within the hypervisor.
class Guest {
 public:
  static zx_status_t Create(ktl::unique_ptr<Guest>* out);
  ~Guest();

  Guest(Guest&&) = delete;
  Guest& operator=(Guest&&) = delete;
  Guest(const Guest&) = delete;
  Guest& operator=(const Guest&) = delete;

  zx_status_t SetTrap(uint32_t kind, zx_vaddr_t addr, size_t len, fbl::RefPtr<PortDispatcher> port,
                      uint64_t key);

  hypervisor::GuestPhysicalAddressSpace& AddressSpace() { return gpas_; }
  hypervisor::TrapMap& Traps() { return traps_; }
  zx_paddr_t MsrBitmapsAddress() const { return msr_bitmaps_page_.PhysicalAddress(); }

  zx::status<hypervisor::Id<uint16_t>> AllocVpid() { return vpid_allocator_.TryAlloc(); }
  zx::status<> FreeVpid(hypervisor::Id<uint16_t> id) { return vpid_allocator_.Free(ktl::move(id)); }
  template <typename F>
  void MigrateVpid(hypervisor::Id<uint16_t>& id, F invalidate) {
    vpid_allocator_.Migrate(id, invalidate);
  }

 private:
  hypervisor::GuestPhysicalAddressSpace gpas_;
  hypervisor::TrapMap traps_;
  hypervisor::IdAllocator<uint16_t, kMaxGuestVcpus> vpid_allocator_;
  VmxPage msr_bitmaps_page_;

  Guest() = default;
};

// Stores the local APIC state for a virtual CPU.
struct LocalApicState {
  // Timer for APIC timer.
  Timer timer;
  // Tracks pending interrupts.
  hypervisor::InterruptTracker<X86_INT_COUNT> interrupt_tracker;
  // LVT timer configuration
  uint32_t lvt_timer = LVT_MASKED;  // Initial state is masked (Vol 3 Section 10.12.5.1).
  uint32_t lvt_initial_count;
  uint32_t lvt_divide_config;
};

// Stores the para-virtualized clock for a virtual CPU.
//
// System time is time since boot time, and boot time is some fixed point in the
// past.
struct pv_clock_system_time;
struct PvClockState {
  bool is_stable = false;
  uint32_t version = 0;
  pv_clock_system_time* system_time = nullptr;
  hypervisor::GuestPtr guest_ptr;
};

// Represents a virtual CPU within a guest.
class Vcpu {
 public:
  static zx_status_t Create(Guest* guest, zx_vaddr_t entry, ktl::unique_ptr<Vcpu>* out);
  ~Vcpu();
  DISALLOW_COPY_ASSIGN_AND_MOVE(Vcpu);

  zx_status_t Enter(zx_port_packet_t* packet);
  void Kick();
  void Interrupt(uint32_t vector);
  zx_status_t ReadState(zx_vcpu_state_t* vcpu_state);
  zx_status_t WriteState(const zx_vcpu_state_t& vcpu_state);
  zx_status_t WriteState(const zx_vcpu_io_t& io_state);

  void GetInfo(zx_info_vcpu_t* info);

 private:
  Guest* const guest_;
  hypervisor::Id<uint16_t> vpid_;
  // |last_cpu_| contains the CPU dedicated to holding the guest's VMCS state,
  // or INVALID_CPU if there is no such VCPU. If this Vcpu is actively running,
  // then |last_cpu_| will point to that CPU.
  //
  // The VMCS state of this Vcpu must not be loaded prior to |last_cpu_| being
  // set, nor must |last_cpu_| be modified prior to the VMCS state being cleared.
  cpu_num_t last_cpu_ TA_GUARDED(ThreadLock::Get());
  // |thread_| will be set to nullptr when the thread exits.
  ktl::atomic<Thread*> thread_;
  ktl::atomic<bool> kicked_ = false;
  LocalApicState local_apic_state_;
  PvClockState pv_clock_state_;
  VmxPage host_msr_page_;
  VmxPage guest_msr_page_;
  VmxPage vmcs_page_;
  VmxState vmx_state_;
  // The guest may enable any state, so the XSAVE area is the maximum size.
  alignas(64) uint8_t extended_register_state_[X86_MAX_EXTENDED_REGISTER_SIZE];

  Vcpu(Guest* guest, hypervisor::Id<uint16_t>& vpid, Thread* thread);

  void MigrateCpu(Thread* thread, Thread::MigrateStage stage) TA_REQ(ThreadLock::Get());
  void LoadExtendedRegisters(Thread* thread, AutoVmcs& vmcs);
  void SaveExtendedRegisters(Thread* thread, AutoVmcs& vmcs);
};

#endif  // ZIRCON_KERNEL_ARCH_X86_INCLUDE_ARCH_HYPERVISOR_H_
