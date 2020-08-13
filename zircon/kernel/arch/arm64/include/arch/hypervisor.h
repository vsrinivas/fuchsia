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
#include <hypervisor/guest_physical_address_space.h>
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

// Stores the state of the GICH across VM exits.
class GichState {
 public:
  zx_status_t Init();

  bool Pending() { return interrupt_tracker_.Pending(); }
  hypervisor::InterruptType Pop(uint32_t* vector) { return interrupt_tracker_.Pop(vector); }
  void Track(uint32_t vector, hypervisor::InterruptType type) {
    interrupt_tracker_.Track(vector, type);
  }
  void Interrupt(uint32_t vector, hypervisor::InterruptType type) {
    interrupt_tracker_.Interrupt(vector, type);
  }
  zx_status_t Wait(zx_time_t deadline) { return interrupt_tracker_.Wait(deadline, nullptr); }

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
  Guest* const guest_;
  const uint8_t vpid_;
  ktl::atomic<Thread*> thread_;
  ktl::atomic<bool> running_;
  ktl::atomic<cpu_num_t> last_cpu_;
  // We allocate El2State in its own page as it is passed between EL1 and EL2,
  // which have different address space mappings. This ensures that El2State
  // will not cross a page boundary and be incorrectly accessed in EL2.
  hypervisor::PagePtr<El2State> el2_state_;
  GichState gich_state_;
  uint64_t hcr_;

  Vcpu(Guest* guest, uint8_t vpid, Thread* thread);

  void MigrateCpu(Thread* thread, Thread::MigrateStage stage) TA_REQ(ThreadLock::Get());
};

#endif  // ZIRCON_KERNEL_ARCH_ARM64_INCLUDE_ARCH_HYPERVISOR_H_
