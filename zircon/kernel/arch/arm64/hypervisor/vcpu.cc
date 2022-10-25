// Copyright 2017 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <bits.h>
#include <lib/fit/defer.h>
#include <lib/ktrace.h>
#include <zircon/errors.h>
#include <zircon/syscalls/hypervisor.h>

#include <arch/hypervisor.h>
#include <arch/ops.h>
#include <dev/interrupt/arm_gic_common.h>
#include <dev/interrupt/arm_gic_hw_interface.h>
#include <hypervisor/aspace.h>
#include <hypervisor/cpu.h>
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

static uint64_t vmpidr_of(uint16_t vpid) {
  constexpr uint64_t res1 = 1ul << 31;
  return (vpid - 1) | res1;
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
    uint32_t vector = 0;
    bool pending = gich_state->Pop(&vector);
    if (!pending) {
      // There are no more pending interrupts.
      break;
    }
    uint32_t lr_index = __builtin_ctzl(elrsr);
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

    ich_state->lr[lr_index] = gic_get_lr_from_vector(prio, state, vector);
    elrsr &= ~(1u << lr_index);
  }
}

GichState::GichState() {
  zx_status_t status = lr_tracker_.Reset(kNumInterrupts);
  // `lr_tracker_` uses static storage, so `Reset` cannot fail.
  DEBUG_ASSERT(status == ZX_OK);
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
zx::result<ktl::unique_ptr<Vcpu>> Vcpu::Create(Guest& guest, zx_vaddr_t entry) {
  hypervisor::GuestPhysicalAspace& gpa = guest.AddressSpace();
  if (entry >= gpa.size()) {
    return zx::error(ZX_ERR_INVALID_ARGS);
  }

  Thread* thread = Thread::Current::Get();
  if (thread->vcpu()) {
    return zx::error(ZX_ERR_BAD_STATE);
  }

  auto vpid = guest.AllocVpid();
  if (vpid.is_error()) {
    return vpid.take_error();
  }

  fbl::AllocChecker ac;
  ktl::unique_ptr<Vcpu> vcpu(new (&ac) Vcpu(guest, *vpid, thread));
  if (!ac.check()) {
    auto result = guest.FreeVpid(*vpid);
    ASSERT(result.is_ok());
    return zx::error(ZX_ERR_NO_MEMORY);
  }

  if (auto result = vcpu->el2_state_.Alloc(); result.is_error()) {
    return result.take_error();
  }

  vcpu->el2_state_->guest_state.system_state.elr_el2 = entry;
  vcpu->el2_state_->guest_state.system_state.spsr_el2 = kSpsrDaif | kSpsrEl1h;
  vcpu->el2_state_->guest_state.vmpidr_el2 = vmpidr_of(*vpid);
  const uint8_t num_lrs = gic_get_num_lrs();
  vcpu->el2_state_->ich_state.num_aprs = num_aprs(gic_get_num_pres());
  vcpu->el2_state_->ich_state.num_lrs = num_lrs;
  vcpu->el2_state_->ich_state.vmcr = gic_default_gich_vmcr();
  vcpu->el2_state_->ich_state.elrsr = (1ul << num_lrs) - 1;
  vcpu->hcr_ = HCR_EL2_VM | HCR_EL2_PTW | HCR_EL2_FMO | HCR_EL2_IMO | HCR_EL2_AMO | HCR_EL2_TWI |
               HCR_EL2_TWE | HCR_EL2_TSC | HCR_EL2_TSW | HCR_EL2_TVM | HCR_EL2_RW;

  return zx::ok(ktl::move(vcpu));
}

Vcpu::Vcpu(Guest& guest, uint16_t vpid, Thread* thread)
    : guest_(guest), vpid_(vpid), last_cpu_(thread->LastCpu()), thread_(thread) {
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
    Guard<MonitoredSpinLock, IrqSave> guard{ThreadLock::Get(), SOURCE_TAG};
    Thread* thread = thread_.load();
    if (thread != nullptr) {
      thread->set_vcpu(false);
      // Clear the migration function, so that |thread_| does not reference
      // |this| after destruction of the VCPU.
      thread->SetMigrateFnLocked(nullptr);
    }
  }

  auto result = guest_.FreeVpid(vpid_);
  ZX_ASSERT(result.is_ok());
}

void Vcpu::MigrateCpu(Thread* thread, Thread::MigrateStage stage) {
  switch (stage) {
    case Thread::MigrateStage::Before:
      last_cpu_ = INVALID_CPU;
      break;
    case Thread::MigrateStage::After:
      // After thread migration, update the |last_cpu_| for Vcpu::Interrupt().
      DEBUG_ASSERT(last_cpu_ == INVALID_CPU);
      last_cpu_ = thread->LastCpuLocked();
      break;
    case Thread::MigrateStage::Exiting:
      // The |thread_| is exiting and so we must clear our reference to it.
      thread_.store(nullptr);
      break;
  }
}

zx_status_t Vcpu::Enter(zx_port_packet_t& packet) {
  Thread* current_thread = Thread::Current::Get();
  if (current_thread != thread_) {
    return ZX_ERR_BAD_STATE;
  }

  const ArchVmAspace& arch_aspace = guest_.AddressSpace().arch_aspace();
  uint64_t vttbr = arm64_vttbr(arch_aspace.arch_asid(), arch_aspace.arch_table_phys());
  GuestState* guest_state = &el2_state_->guest_state;
  IchState* ich_state = &el2_state_->ich_state;
  zx_status_t status;
  do {
    // If the thread was killed or suspended, then we should exit with an error.
    status = current_thread->CheckKillOrSuspendSignal();
    if (status != ZX_OK) {
      return status;
    }
    timer_maybe_interrupt(guest_state, &gich_state_);
    gich_maybe_interrupt(&gich_state_, ich_state);
    {
      AutoGich auto_gich(ich_state, gich_state_.Pending());

      // We check whether a kick was requested before entering the guest so that:
      // 1. When we enter the syscall, we can return immediately without entering
      //    the guest.
      // 2. If we have already exited the guest to handle a packet, it allows us
      //    to return and gives user-space a chance to handle that packet, without
      //    the request to kick interfering with the packet in-flight.
      //
      // We also do this after we have disabled interrupts, so if an interrupt was
      // fired before we disabled interrupts, we have the opportunity to check
      // whether a kick was requested, but the interrupt was lost. If an interrupt
      // is fired after we have disabled interrupts, when we enter the guest we
      // will exit due to the interrupt, and run this check again.
      if (kicked_.exchange(false)) {
        return ZX_ERR_CANCELED;
      }

      ktrace(TAG_VCPU_ENTER, 0, 0, 0, 0);
      GUEST_STATS_INC(vm_entries);
      status = arm64_el2_enter(vttbr, el2_state_.PhysicalAddress(), hcr_);
      GUEST_STATS_INC(vm_exits);
    }
    gich_state_.TrackAllListRegisters(ich_state);
    if (status == ZX_ERR_NEXT) {
      // We received a physical interrupt. Continue execution of the guest.
      ktrace_vcpu_exit(vmexit_interrupt_ktrace_meta(ich_state->misr),
                       guest_state->system_state.elr_el2);
      GUEST_STATS_INC(interrupts);
      status = ZX_OK;
    } else if (status == ZX_OK) {
      status = vmexit_handler(&hcr_, guest_state, &gich_state_, &guest_.AddressSpace(),
                              &guest_.Traps(), &packet);
    } else {
      ktrace_vcpu_exit(VCPU_FAILURE, guest_state->system_state.elr_el2);
      dprintf(INFO, "hypervisor: VCPU enter failed: %d\n", status);
    }
  } while (status == ZX_OK);
  return status == ZX_ERR_NEXT ? ZX_OK : status;
}

void Vcpu::Kick() {
  kicked_.store(true);
  // Cancel any pending or upcoming wait-for-interrupts.
  gich_state_.Cancel();
  // Check if the VCPU is running and whether to send an IPI. We hold the thread
  // lock to guard against thread migration between CPUs during the check.
  //
  // NOTE: `last_cpu_` may be currently set to `INVALID_CPU` due to thread
  // migration between CPUs.
  Guard<MonitoredSpinLock, IrqSave> guard{ThreadLock::Get(), SOURCE_TAG};
  auto t = thread_.load();
  if (t != nullptr && t->state() == THREAD_RUNNING && last_cpu_ != INVALID_CPU) {
    mp_interrupt(MP_IPI_TARGET_MASK, cpu_num_to_mask(last_cpu_));
  }
}

void Vcpu::Interrupt(uint32_t vector) {
  gich_state_.Interrupt(vector);
  // Check if the VCPU is running and whether to send an IPI. We hold the thread
  // lock to guard against thread migration between CPUs during the check.
  //
  // NOTE: `last_cpu_` may be currently set to `INVALID_CPU` due to thread
  // migration between CPUs.
  Guard<MonitoredSpinLock, IrqSave> guard{ThreadLock::Get(), SOURCE_TAG};
  auto t = thread_.load();
  if (t != nullptr && t->state() == THREAD_RUNNING && last_cpu_ != INVALID_CPU) {
    mp_interrupt(MP_IPI_TARGET_MASK, cpu_num_to_mask(last_cpu_));
  }
}

zx_status_t Vcpu::ReadState(zx_vcpu_state_t& state) const {
  if (Thread::Current::Get() != thread_) {
    return ZX_ERR_BAD_STATE;
  }

  ASSERT(sizeof(state.x) >= sizeof(el2_state_->guest_state.x));
  memcpy(state.x, el2_state_->guest_state.x, sizeof(el2_state_->guest_state.x));
  state.sp = el2_state_->guest_state.system_state.sp_el1;
  state.cpsr = el2_state_->guest_state.system_state.spsr_el2 & kSpsrNzcv;
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

void Vcpu::GetInfo(zx_info_vcpu_t* info) {
  if (kicked_.load()) {
    info->flags |= ZX_INFO_VCPU_FLAG_KICKED;
  }
}
