// Copyright 2017 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <bits.h>
#include <lib/ktrace.h>
#include <zircon/errors.h>
#include <zircon/syscalls/hypervisor.h>

#include <arch/hypervisor.h>
#include <arch/ops.h>
#include <dev/interrupt/arm_gic_common.h>
#include <dev/interrupt/arm_gic_hw_interface.h>
#include <fbl/auto_call.h>
#include <hypervisor/cpu.h>
#include <hypervisor/guest_physical_address_space.h>
#include <hypervisor/ktrace.h>
#include <kernel/event.h>
#include <kernel/percpu.h>
#include <kernel/stats.h>
#include <platform/timer.h>
#include <vm/physmap.h>
#include <vm/pmm.h>

#include "el2_cpu_state_priv.h"
#include "vmexit_priv.h"

static constexpr uint32_t kGichHcrEn = 1u << 0;
static constexpr uint32_t kGichHcrUie = 1u << 1;
static constexpr uint32_t kGichMisrU = 1u << 1;
static constexpr uint32_t kSpsrDaif = 0b1111 << 6;
static constexpr uint32_t kSpsrEl1h = 0b0101;
static constexpr uint32_t kSpsrNzcv = 0b1111 << 28;

static uint64_t vmpidr_of(uint8_t vpid, uint64_t mpidr) {
  return (vpid - 1) | (mpidr & 0xffffff00fe000000);
}

template <typename F>
static bool for_each_lr(IchState* ich_state, F function) {
  for (uint8_t i = 0; i < ich_state->num_lrs; i++) {
    if (BIT(ich_state->elrsr, i)) {
      continue;
    }
    InterruptState state;
    uint32_t vector = gic_get_vector_from_lr(ich_state->lr[i], &state);
    zx_status_t status = function(i, state, vector);
    if (status == ZX_ERR_STOP) {
      return false;
    }
  }
  return true;
}

static void gich_maybe_interrupt(GichState* gich_state, IchState* ich_state) {
  // From ARM GIC v3/v4, Section 4.8: If, on a particular CPU interface,
  // multiple pending interrupts have the same priority, and have sufficient
  // priority for the interface to signal them to the PE, it is IMPLEMENTATION
  // DEFINED how the interface selects which interrupt to signal.
  //
  // If interrupts are of the same priority, we can choose whatever ordering
  // we prefer when populating the LRs.
  for (uint64_t elrsr = ich_state->elrsr; elrsr != 0;) {
    uint32_t vector;
    hypervisor::InterruptType type = gich_state->Pop(&vector);
    if (type == hypervisor::InterruptType::INACTIVE) {
      // There are no more pending interrupts.
      break;
    }
    uint32_t lr_index = __builtin_ctzl(elrsr);
    bool hw = type == hypervisor::InterruptType::PHYSICAL;
    // From ARM GIC v3/v4, Section 4.8: If the GIC implements fewer than 256
    // priority levels, the low-order bits of the priority fields are
    // RAZ/WI.
    // ...
    // In the GIC prioritization scheme, lower numbers have higher priority.
    //
    // We may have as few as 16 priority levels, so step by 16 to the next
    // lowest priority in order to prioritise SGIs and PPIs over SPIs.
    uint8_t prio = vector < GIC_BASE_SPI ? 0 : 0x10;
    InterruptState state = InterruptState::PENDING;

    if (gich_state->InListRegister(vector)) {
      bool skip = for_each_lr(ich_state, [&](uint8_t i, InterruptState s, uint32_t v) {
        if (v != vector || s != InterruptState::ACTIVE) {
          return ZX_ERR_NEXT;
        }
        // If the interrupt is active, change its state to pending and active.
        state = InterruptState::PENDING_AND_ACTIVE;
        lr_index = i;
        return ZX_ERR_STOP;
      });
      if (skip) {
        // Skip an interrupt if it is in an LR, and its state is not changing.
        continue;
      }
    }

    ich_state->lr[lr_index] = gic_get_lr_from_vector(hw, prio, state, vector);
    elrsr &= ~(1u << lr_index);
  }
}

zx_status_t GichState::Init() {
  zx_status_t status = interrupt_tracker_.Init();
  if (status != ZX_OK) {
    return status;
  }
  return lr_tracker_.Reset(kNumInterrupts);
}

void GichState::TrackAllListRegisters(IchState* ich_state) {
  lr_tracker_.ClearAll();
  for_each_lr(ich_state, [this](uint8_t i, InterruptState s, uint32_t v) {
    lr_tracker_.SetOne(v);
    return ZX_ERR_NEXT;
  });
}

static VcpuExit vmexit_interrupt_ktrace_meta(uint32_t misr) {
  if (misr & kGichMisrU) {
    return VCPU_UNDERFLOW_MAINTENANCE_INTERRUPT;
  }
  return VCPU_PHYSICAL_INTERRUPT;
}

AutoGich::AutoGich(IchState* ich_state, bool pending) : ich_state_(ich_state) {
  // From ARM GIC v3/v4, Section 8.4.5: Underflow Interrupt Enable. Enables
  // the signaling of a maintenance interrupt when the List registers are
  // empty, or hold only one valid entry.
  //
  // We use it when there are not enough free LRs to inject all pending
  // interrupts, so when guest finishes processing most of them, a maintenance
  // interrupt will cause VM exit and will give us a chance to inject the
  // remaining interrupts. The point of this is to reduce latency when
  // processing interrupts.
  uint32_t gich_hcr = kGichHcrEn;
  if (pending && ich_state_->num_lrs > 1) {
    gich_hcr |= kGichHcrUie;
  }

  DEBUG_ASSERT(!arch_ints_disabled());
  int_state_ = arch_interrupt_save();
  arch_set_blocking_disallowed(true);
  gic_write_gich_state(ich_state_, gich_hcr);
}

AutoGich::~AutoGich() {
  DEBUG_ASSERT(arch_ints_disabled());
  gic_read_gich_state(ich_state_);
  arch_set_blocking_disallowed(false);
  arch_interrupt_restore(int_state_);
}

// Returns the number of active priorities registers, based on the number of
// preemption bits.
//
// From ARM GIC v2, Section 5.3.2: In GICv2, the only valid value is 5 bits.
//
// From ARM GIC v3/v4, Section 8.4.2: If 5 bits of preemption are implemented
// (bits [7:3] of priority), then there are 32 preemption levels... If 6 bits of
// preemption are implemented (bits [7:2] of priority), then there are 64
// preemption levels... If 7 bits of preemption are implemented (bits [7:1] of
// priority), then there are 128 preemption levels...
static uint8_t num_aprs(uint8_t num_pres) { return static_cast<uint8_t>(1u << (num_pres - 5u)); }

// static
zx_status_t Vcpu::Create(Guest* guest, zx_vaddr_t entry, ktl::unique_ptr<Vcpu>* out) {
  hypervisor::GuestPhysicalAddressSpace* gpas = guest->AddressSpace();
  if (entry >= gpas->size()) {
    return ZX_ERR_INVALID_ARGS;
  }

  uint8_t vpid;
  zx_status_t status = guest->AllocVpid(&vpid);
  if (status != ZX_OK) {
    return status;
  }
  auto auto_call = fbl::MakeAutoCall([guest, vpid]() { guest->FreeVpid(vpid); });

  Thread* thread = Thread::Current::Get();
  if (thread->vcpu()) {
    return ZX_ERR_BAD_STATE;
  }

  fbl::AllocChecker ac;
  ktl::unique_ptr<Vcpu> vcpu(new (&ac) Vcpu(guest, vpid, thread));
  if (!ac.check()) {
    return ZX_ERR_NO_MEMORY;
  }
  auto_call.cancel();

  status = vcpu->el2_state_.Alloc();
  if (status != ZX_OK) {
    return status;
  }
  status = vcpu->gich_state_.Init();
  if (status != ZX_OK) {
    return status;
  }

  vcpu->el2_state_->guest_state.system_state.elr_el2 = entry;
  vcpu->el2_state_->guest_state.system_state.spsr_el2 = kSpsrDaif | kSpsrEl1h;
  const uint64_t mpidr = __arm_rsr64("mpidr_el1");
  vcpu->el2_state_->guest_state.system_state.vmpidr_el2 = vmpidr_of(vpid, mpidr);
  vcpu->el2_state_->host_state.system_state.vmpidr_el2 = mpidr;
  const uint8_t num_lrs = gic_get_num_lrs();
  vcpu->el2_state_->ich_state.num_aprs = num_aprs(gic_get_num_pres());
  vcpu->el2_state_->ich_state.num_lrs = num_lrs;
  vcpu->el2_state_->ich_state.vmcr = gic_default_gich_vmcr();
  vcpu->el2_state_->ich_state.elrsr = (1ul << num_lrs) - 1;
  vcpu->hcr_ = HCR_EL2_VM | HCR_EL2_PTW | HCR_EL2_FMO | HCR_EL2_IMO | HCR_EL2_DC | HCR_EL2_TWI |
               HCR_EL2_TWE | HCR_EL2_TSC | HCR_EL2_TVM | HCR_EL2_RW;

  *out = ktl::move(vcpu);
  return ZX_OK;
}

Vcpu::Vcpu(Guest* guest, uint8_t vpid, Thread* thread)
    : guest_(guest), vpid_(vpid), thread_(thread), last_cpu_(thread->LastCpu()) {
  thread->set_vcpu(true);
  // We have to disable thread safety analysis because it's not smart enough to
  // realize that SetMigrateFn will always be called with the ThreadLock.
  thread->SetMigrateFn([this](Thread* thread, auto stage)
                           TA_NO_THREAD_SAFETY_ANALYSIS { MigrateCpu(thread, stage); });
}

Vcpu::~Vcpu() {
  {
    // Taking the ThreadLock guarantees that thread_ isn't going to be freed
    // while we access it.
    Guard<SpinLock, IrqSave> guard{ThreadLock::Get()};
    Thread* thread = thread_.load();
    if (thread != nullptr) {
      thread->set_vcpu(false);
      // Clear the migration function, so that |thread_| does not reference
      // |this| after destruction of the VCPU.
      thread->SetMigrateFnLocked(nullptr);
    }
  }

  __UNUSED zx_status_t status = guest_->FreeVpid(vpid_);
  DEBUG_ASSERT(status == ZX_OK);
}

void Vcpu::MigrateCpu(Thread* thread, Thread::MigrateStage stage) {
  switch (stage) {
    case Thread::MigrateStage::Before:
      break;
    case Thread::MigrateStage::After:
      // After thread migration, update the |last_cpu_| for Vcpu::Interrupt().
      last_cpu_.store(thread->LastCpuLocked());
      break;
    case Thread::MigrateStage::Exiting:
      // When the thread is exiting, set |last_cpu_| to INVALID_CPU and
      // |thread_| to nullptr.
      last_cpu_.store(INVALID_CPU);
      thread_.store(nullptr);
      break;
  }
}

zx_status_t Vcpu::Resume(zx_port_packet_t* packet) {
  Thread* current_thread = Thread::Current::Get();
  if (current_thread != thread_) {
    return ZX_ERR_BAD_STATE;
  }

  const ArchVmAspace& aspace = *guest_->AddressSpace()->arch_aspace();
  zx_paddr_t vttbr = arm64_vttbr(aspace.arch_asid(), aspace.arch_table_phys());
  GuestState* guest_state = &el2_state_->guest_state;
  IchState* ich_state = &el2_state_->ich_state;
  zx_status_t status;
  do {
    timer_maybe_interrupt(guest_state, &gich_state_);
    gich_maybe_interrupt(&gich_state_, ich_state);
    {
      AutoGich auto_gich(ich_state, gich_state_.Pending());

      ktrace(TAG_VCPU_ENTER, 0, 0, 0, 0);
      GUEST_STATS_INC(vm_entries);
      running_.store(true);
      status = arm64_el2_resume(vttbr, el2_state_.PhysicalAddress(), hcr_);
      running_.store(false);
      GUEST_STATS_INC(vm_exits);
    }
    gich_state_.TrackAllListRegisters(ich_state);
    if (status == ZX_ERR_NEXT) {
      // We received a physical interrupt. If it was due to the thread
      // being killed, then we should exit with an error, otherwise return
      // to the guest.
      ktrace_vcpu_exit(vmexit_interrupt_ktrace_meta(ich_state->misr),
                       guest_state->system_state.elr_el2);
      status = current_thread->signals() & THREAD_SIGNAL_KILL ? ZX_ERR_CANCELED : ZX_OK;
      GUEST_STATS_INC(interrupts);
    } else if (status == ZX_OK) {
      status = vmexit_handler(&hcr_, guest_state, &gich_state_, guest_->AddressSpace(),
                              guest_->Traps(), packet);
    } else {
      ktrace_vcpu_exit(VCPU_FAILURE, guest_state->system_state.elr_el2);
      dprintf(INFO, "VCPU resume failed: %d\n", status);
    }
  } while (status == ZX_OK);
  return status == ZX_ERR_NEXT ? ZX_OK : status;
}

void Vcpu::Interrupt(uint32_t vector, hypervisor::InterruptType type) {
  gich_state_.Interrupt(vector, type);
  if (running_.load()) {
    mp_interrupt(MP_IPI_TARGET_MASK, cpu_num_to_mask(last_cpu_.load()));
  }
}

zx_status_t Vcpu::ReadState(zx_vcpu_state_t* state) const {
  if (Thread::Current::Get() != thread_) {
    return ZX_ERR_BAD_STATE;
  }

  ASSERT(sizeof(state->x) >= sizeof(el2_state_->guest_state.x));
  memcpy(state->x, el2_state_->guest_state.x, sizeof(el2_state_->guest_state.x));
  state->sp = el2_state_->guest_state.system_state.sp_el1;
  state->cpsr = el2_state_->guest_state.system_state.spsr_el2 & kSpsrNzcv;
  return ZX_OK;
}

zx_status_t Vcpu::WriteState(const zx_vcpu_state_t& state) {
  if (Thread::Current::Get() != thread_) {
    return ZX_ERR_BAD_STATE;
  }

  ASSERT(sizeof(el2_state_->guest_state.x) >= sizeof(state.x));
  memcpy(el2_state_->guest_state.x, state.x, sizeof(state.x));
  el2_state_->guest_state.system_state.sp_el1 = state.sp;
  el2_state_->guest_state.system_state.spsr_el2 |= state.cpsr & kSpsrNzcv;
  return ZX_OK;
}
