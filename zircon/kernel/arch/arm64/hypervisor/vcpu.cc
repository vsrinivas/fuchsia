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
#include <kernel/mp.h>
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
    } else if (gich_state->GetInterruptState(vector) != InterruptState::INACTIVE) {
      // Skip an interrupt if it was already active or pending.
      continue;
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
    uint64_t lr = gic_get_lr_from_vector(hw, prio, InterruptState::PENDING, vector);
    ich_state->lr[lr_index] = lr;
    elrsr &= ~(1u << lr_index);
  }
}

zx_status_t GichState::Init() {
  current_interrupts_.Reset(kNumBits);
  return interrupt_tracker_.Init();
}

InterruptState GichState::GetInterruptState(uint32_t vector) {
  if (vector >= kNumInterrupts) {
    DEBUG_ASSERT(false);
    return InterruptState::INACTIVE;
  }
  size_t bitoff = vector * 2;
  uint32_t state =
      (current_interrupts_.GetOne(bitoff) << 0) | (current_interrupts_.GetOne(bitoff + 1) << 1);
  return static_cast<InterruptState>(state);
}

bool GichState::HasPendingInterrupt() {
  size_t start = 0;
  while (start < kNumBits) {
    size_t bitoff;
    bool is_empty = current_interrupts_.Scan(start, kNumBits, false, &bitoff);
    if (is_empty) {
      return false;
    } else if (bitoff % 2 == 0) {
      return true;
    }
    start = bitoff + 1;
  }
  return false;
}

void GichState::SetAllInterruptStates(IchState* ich_state) {
  current_interrupts_.ClearAll();
  for (uint32_t i = 0; i < ich_state->num_lrs; i++) {
    if (BIT(ich_state->elrsr, i)) {
      continue;
    }
    InterruptState state;
    uint32_t vector = gic_get_vector_from_lr(ich_state->lr[i], &state);
    SetInterruptState(vector, state);
  }
}

// This function must only be called by SetAllInterruptStates, as it
// assumes |current_interrupts_| has been cleared.
void GichState::SetInterruptState(uint32_t vector, InterruptState state) {
  if (vector >= kNumInterrupts) {
    DEBUG_ASSERT(false);
    return;
  }
  size_t bitoff = vector * 2;
  if (state == InterruptState::PENDING || state == InterruptState::PENDING_AND_ACTIVE) {
    current_interrupts_.SetOne(bitoff);
  }
  if (state == InterruptState::ACTIVE || state == InterruptState::PENDING_AND_ACTIVE) {
    current_interrupts_.SetOne(bitoff + 1);
  }
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
  arch_disable_ints();
  arch_set_blocking_disallowed(true);
  gic_write_gich_state(ich_state_, gich_hcr);
}

AutoGich::~AutoGich() {
  DEBUG_ASSERT(arch_ints_disabled());
  gic_read_gich_state(ich_state_);
  arch_set_blocking_disallowed(false);
  arch_enable_ints();
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

  // For efficiency, we pin the thread to the CPU.
  Thread* thread = hypervisor::pin_thread(vpid);

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

Vcpu::Vcpu(Guest* guest, uint8_t vpid, const Thread* thread)
    : guest_(guest), vpid_(vpid), thread_(thread), running_(false) {}

Vcpu::~Vcpu() {
  __UNUSED zx_status_t status = guest_->FreeVpid(vpid_);
  DEBUG_ASSERT(status == ZX_OK);
}

zx_status_t Vcpu::Resume(zx_port_packet_t* packet) {
  if (!hypervisor::check_pinned_cpu_invariant(vpid_, thread_)) {
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
      running_.store(true);
      GUEST_STATS_INC(vm_entries);
      status = arm64_el2_resume(vttbr, el2_state_.PhysicalAddress(), hcr_);
      GUEST_STATS_INC(vm_exits);
      running_.store(false);
    }
    gich_state_.SetAllInterruptStates(ich_state);
    if (status == ZX_ERR_NEXT) {
      // We received a physical interrupt. If it was due to the thread
      // being killed, then we should exit with an error, otherwise return
      // to the guest.
      ktrace_vcpu_exit(vmexit_interrupt_ktrace_meta(ich_state->misr),
                       guest_state->system_state.elr_el2);
      status = thread_->signals_ & THREAD_SIGNAL_KILL ? ZX_ERR_CANCELED : ZX_OK;
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

cpu_mask_t Vcpu::Interrupt(uint32_t vector, hypervisor::InterruptType type) {
  bool signaled = false;
  gich_state_.Interrupt(vector, type, &signaled);
  if (signaled || !running_.load()) {
    return 0;
  }
  return cpu_num_to_mask(hypervisor::cpu_of(vpid_));
}

void Vcpu::VirtualInterrupt(uint32_t vector) {
  cpu_mask_t mask = Interrupt(vector, hypervisor::InterruptType::VIRTUAL);
  if (mask != 0) {
    mp_interrupt(MP_IPI_TARGET_MASK, mask);
  }
}

zx_status_t Vcpu::ReadState(zx_vcpu_state_t* state) const {
  if (!hypervisor::check_pinned_cpu_invariant(vpid_, thread_)) {
    return ZX_ERR_BAD_STATE;
  }

  ASSERT(sizeof(state->x) >= sizeof(el2_state_->guest_state.x));
  memcpy(state->x, el2_state_->guest_state.x, sizeof(el2_state_->guest_state.x));
  state->sp = el2_state_->guest_state.system_state.sp_el1;
  state->cpsr = el2_state_->guest_state.system_state.spsr_el2 & kSpsrNzcv;
  return ZX_OK;
}

zx_status_t Vcpu::WriteState(const zx_vcpu_state_t& state) {
  if (!hypervisor::check_pinned_cpu_invariant(vpid_, thread_)) {
    return ZX_ERR_BAD_STATE;
  }

  ASSERT(sizeof(el2_state_->guest_state.x) >= sizeof(state.x));
  memcpy(el2_state_->guest_state.x, state.x, sizeof(state.x));
  el2_state_->guest_state.system_state.sp_el1 = state.sp;
  el2_state_->guest_state.system_state.spsr_el2 |= state.cpsr & kSpsrNzcv;
  return ZX_OK;
}
