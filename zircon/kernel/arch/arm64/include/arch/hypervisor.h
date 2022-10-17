// Copyright 2017 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#ifndef ZIRCON_KERNEL_ARCH_ARM64_INCLUDE_ARCH_HYPERVISOR_H_
#define ZIRCON_KERNEL_ARCH_ARM64_INCLUDE_ARCH_HYPERVISOR_H_

#include <zircon/syscalls/hypervisor.h>
#include <zircon/types.h>

#include <arch/arm64/hypervisor/el2_state.h>
#include <fbl/ref_ptr.h>
#include <hypervisor/aspace.h>
#include <hypervisor/id_allocator.h>
#include <hypervisor/interrupt_tracker.h>
#include <hypervisor/page.h>
#include <hypervisor/trap_map.h>
#include <kernel/event.h>
#include <kernel/spinlock.h>
#include <ktl/unique_ptr.h>

static constexpr uint8_t kNumGroups = 2;
// See CoreLink GIC-400, Section 2.3.2 PPIs.
static constexpr uint32_t kMaintenanceVector = 25;
static constexpr uint32_t kTimerVector = 27;
static constexpr uint16_t kNumInterrupts = 256;
static_assert(kMaintenanceVector < kNumInterrupts, "Maintenance vector is out of range");
static_assert(kTimerVector < kNumInterrupts, "Timer vector is out of range");

// Maximum VCPUs per guest.
constexpr size_t kMaxGuestVcpus = 8;

typedef struct zx_port_packet zx_port_packet_t;
class PortDispatcher;
enum class InterruptState : uint8_t;

// Represents a guest within the hypervisor.
class Guest {
 public:
  static zx::result<ktl::unique_ptr<Guest>> Create();
  ~Guest();

  Guest(Guest&&) = delete;
  Guest& operator=(Guest&&) = delete;
  Guest(const Guest&) = delete;
  Guest& operator=(const Guest&) = delete;

  zx_status_t SetTrap(uint32_t kind, zx_vaddr_t addr, size_t len, fbl::RefPtr<PortDispatcher> port,
                      uint64_t key);

  hypervisor::GuestPhysicalAspace& AddressSpace() { return gpa_; }
  fbl::RefPtr<VmAddressRegion> RootVmar() const { return gpa_.RootVmar(); }
  hypervisor::TrapMap& Traps() { return traps_; }

  zx::result<uint16_t> AllocVpid() { return vpid_allocator_.TryAlloc(); }
  zx::result<> FreeVpid(uint16_t id) { return vpid_allocator_.Free(id); }

 private:
  uint16_t vmid_;
  hypervisor::GuestPhysicalAspace gpa_;
  hypervisor::TrapMap traps_;
  hypervisor::IdAllocator<uint16_t, kMaxGuestVcpus> vpid_allocator_;

  explicit Guest(uint16_t vmid);
};

using NormalGuest = Guest;

// Stores the state of the GICH across VM exits.
class GichState {
 public:
  GichState();

  bool Pending() { return interrupt_tracker_.Pending(); }
  bool Pop(uint32_t* vector) { return interrupt_tracker_.Pop(vector); }
  void Track(uint32_t vector) { interrupt_tracker_.Track(vector); }
  void Interrupt(uint32_t vector) { interrupt_tracker_.Interrupt(vector); }
  void Cancel() { interrupt_tracker_.Cancel(); }
  zx_status_t Wait(zx_time_t deadline) { return interrupt_tracker_.Wait(deadline).status_value(); }

  bool IsUsingListRegister() { return !lr_tracker_.Scan(0, kNumInterrupts, false); }
  bool InListRegister(uint32_t vector) { return lr_tracker_.GetOne(vector); }
  void TrackAllListRegisters(IchState* ich_state);

 private:
  // Tracks pending interrupts.
  hypervisor::InterruptTracker<kNumInterrupts> interrupt_tracker_;

  // Tracks interrupts in list registers.
  bitmap::RawBitmapGeneric<bitmap::FixedStorage<kNumInterrupts>> lr_tracker_;
};

// Loads GICH within a given scope.
class AutoGich {
 public:
  AutoGich(IchState* ich_state, bool pending);
  ~AutoGich();

 private:
  IchState* ich_state_;
  interrupt_saved_state_t int_state_;
};

// Represents a virtual CPU within a guest.
class Vcpu {
 public:
  static zx::result<ktl::unique_ptr<Vcpu>> Create(Guest& guest, zx_vaddr_t entry);
  ~Vcpu();

  Vcpu(Vcpu&&) = delete;
  Vcpu& operator=(Vcpu&&) = delete;
  Vcpu(const Vcpu&) = delete;
  Vcpu& operator=(const Vcpu&) = delete;

  zx_status_t Enter(zx_port_packet_t& packet);
  void Kick();
  void Interrupt(uint32_t vector);
  zx_status_t ReadState(zx_vcpu_state_t& state) const;
  zx_status_t WriteState(const zx_vcpu_state_t& state);
  zx_status_t WriteState(const zx_vcpu_io_t& io_state) { return ZX_ERR_INVALID_ARGS; }

  void GetInfo(zx_info_vcpu_t* info);

 private:
  Guest& guest_;
  const uint16_t vpid_;
  cpu_num_t last_cpu_ TA_GUARDED(ThreadLock::Get());
  // |thread_| will be set to nullptr when the thread exits.
  ktl::atomic<Thread*> thread_;
  ktl::atomic<bool> kicked_ = false;
  // We allocate El2State in its own page as it is passed between EL1 and EL2,
  // which have different address space mappings. This ensures that El2State
  // will not cross a page boundary and be incorrectly accessed in EL2.
  hypervisor::PagePtr<El2State> el2_state_;
  GichState gich_state_;
  uint64_t hcr_;

  Vcpu(Guest& guest, uint16_t vpid, Thread* thread);

  void MigrateCpu(Thread* thread, Thread::MigrateStage stage) TA_REQ(ThreadLock::Get());
};

using NormalVcpu = Vcpu;

#endif  // ZIRCON_KERNEL_ARCH_ARM64_INCLUDE_ARCH_HYPERVISOR_H_
