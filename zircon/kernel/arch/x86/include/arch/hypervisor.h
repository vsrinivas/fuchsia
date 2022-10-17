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
#include <hypervisor/aspace.h>
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
  Guest(const Guest&) = delete;
  Guest(Guest&&) = delete;
  Guest& operator=(const Guest&) = delete;
  Guest& operator=(Guest&&) = delete;

  virtual ~Guest();

  virtual fbl::RefPtr<VmAddressRegion> RootVmar() const = 0;
  zx_paddr_t MsrBitmapsAddress() const { return msr_bitmaps_page_.PhysicalAddress(); }

 protected:
  template <typename G>
  static zx::result<ktl::unique_ptr<G>> Create();

  Guest() = default;

  VmxPage msr_bitmaps_page_;
};

class NormalGuest : public Guest {
 public:
  static zx::result<ktl::unique_ptr<Guest>> Create();

  zx_status_t SetTrap(uint32_t kind, zx_vaddr_t addr, size_t len, fbl::RefPtr<PortDispatcher> port,
                      uint64_t key);

  zx::result<uint16_t> TryAllocVpid() { return vpid_allocator_.TryAlloc(); }
  zx::result<> FreeVpid(uint16_t vpid) { return vpid_allocator_.Free(vpid); }

  hypervisor::GuestPhysicalAspace& AddressSpace() { return gpa_; }
  fbl::RefPtr<VmAddressRegion> RootVmar() const override { return gpa_.RootVmar(); }
  hypervisor::TrapMap& Traps() { return traps_; }

 private:
  hypervisor::GuestPhysicalAspace gpa_;
  hypervisor::TrapMap traps_;
  hypervisor::IdAllocator<uint16_t, kMaxGuestVcpus> vpid_allocator_;
};

class DirectGuest : public Guest {
 public:
  // Global VPID for a direct mode address space.
  static constexpr uint16_t kGlobalAspaceVpid = 1;

  static zx::result<ktl::unique_ptr<Guest>> Create();
  ~DirectGuest() override;

  hypervisor::DirectPhysicalAspace& AddressSpace() { return dpa_; }
  fbl::RefPtr<VmAddressRegion> RootVmar() const override { return user_aspace_->RootVmar(); }
  VmAspace& user_aspace() { return *user_aspace_; }

 private:
  hypervisor::DirectPhysicalAspace dpa_;
  fbl::RefPtr<VmAspace> user_aspace_;
};

// Represents a virtual CPU within a guest.
class Vcpu {
 public:
  Vcpu(const Vcpu&) = delete;
  Vcpu(Vcpu&&) = delete;
  Vcpu& operator=(const Vcpu&) = delete;
  Vcpu& operator=(Vcpu&&) = delete;

  virtual ~Vcpu();

  virtual zx_status_t Enter(zx_port_packet_t& packet) = 0;
  virtual void Kick() = 0;
  zx_status_t ReadState(zx_vcpu_state_t& vcpu_state);
  zx_status_t WriteState(const zx_vcpu_state_t& vcpu_state);

  void GetInfo(zx_info_vcpu_t* info);

 protected:
  template <typename V, typename G>
  static zx::result<ktl::unique_ptr<V>> Create(G& guest, uint16_t vpid, zx_vaddr_t entry);

  Vcpu(Guest& guest, uint16_t vpid, Thread* thread);

  void MigrateCpu(Thread* thread, Thread::MigrateStage stage) TA_REQ(ThreadLock::Get());
  void LoadExtendedRegisters(AutoVmcs& vmcs);
  void SaveExtendedRegisters(AutoVmcs& vmcs);

  // `PreEnterFn` must have type `(AutoVmcs&) -> zx_status_t`
  // `PostExitFn` must have type `(AutoVmcs&, zx_port_packet_t&) -> zx_status_t`
  template <typename PreEnterFn, typename PostExitFn>
  zx_status_t EnterInternal(PreEnterFn pre_enter, PostExitFn post_exit, zx_port_packet_t& packet);

  Guest& guest_;
  const uint16_t vpid_;
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
  VmxPage host_msr_page_;
  VmxPage guest_msr_page_;
  VmxPage vmcs_page_;
  VmxState vmx_state_;
  // The guest may enable any state, so the XSAVE area is the maximum size.
  alignas(64) uint8_t extended_register_state_[X86_MAX_EXTENDED_REGISTER_SIZE];
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

struct VcpuConfig {
  // Whether there is a base processor for this type of VCPU.
  bool has_base_processor;
  // Whether we VM exit when loading or storing some control registers.
  bool cr_exiting;
  // Whether we may run in unpaged protected mode or in real-address mode.
  bool unrestricted;
};

class NormalVcpu : public Vcpu {
 public:
  static constexpr VcpuConfig kConfig = {
      .has_base_processor = true,
      .cr_exiting = false,
      .unrestricted = true,
  };

  static zx::result<ktl::unique_ptr<Vcpu>> Create(NormalGuest& guest, zx_vaddr_t entry);

  NormalVcpu(NormalGuest& guest, uint16_t vpid, Thread* thread);
  ~NormalVcpu() override;

  zx_status_t Enter(zx_port_packet_t& packet) override;
  void Kick() override;
  void Interrupt(uint32_t vector);
  zx_status_t WriteState(const zx_vcpu_io_t& io_state);

 private:
  LocalApicState local_apic_state_;
  PvClockState pv_clock_state_;
};

class DirectVcpu : public Vcpu {
 public:
  static constexpr VcpuConfig kConfig = {
      .has_base_processor = false,
      .cr_exiting = true,
      .unrestricted = false,
  };

  static zx::result<ktl::unique_ptr<Vcpu>> Create(DirectGuest& guest, zx_vaddr_t entry);

  DirectVcpu(DirectGuest& guest, uint16_t vpid, Thread* thread);

  zx_status_t Enter(zx_port_packet_t& packet) override;
  void Kick() override;

 private:
  VmAspace& SwitchAspace(VmAspace& aspace);

  uintptr_t fs_base_ = 0;
};

#endif  // ZIRCON_KERNEL_ARCH_X86_INCLUDE_ARCH_HYPERVISOR_H_
