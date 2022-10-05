// Copyright 2017 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <bits.h>
#include <lib/affine/ratio.h>
#include <lib/arch/cache.h>
#include <platform.h>
#include <trace.h>
#include <zircon/syscalls/hypervisor.h>
#include <zircon/syscalls/port.h>

#include <arch/arm64/hypervisor/el2_state.h>
#include <arch/hypervisor.h>
#include <dev/interrupt/arm_gic_hw_interface.h>
#include <dev/psci.h>
#include <dev/timer/arm_generic.h>
#include <hypervisor/ktrace.h>
#include <kernel/percpu.h>
#include <kernel/stats.h>
#include <vm/fault.h>
#include <vm/physmap.h>

#include "vmexit_priv.h"

#define LOCAL_TRACE 0

#define SET_SYSREG(sysreg)                                                  \
  ({                                                                        \
    guest_state->system_state.sysreg = reg;                                 \
    LTRACEF("guest " #sysreg ": %#lx\n", guest_state->system_state.sysreg); \
    next_pc(guest_state);                                                   \
    ZX_OK;                                                                  \
  })

namespace {

constexpr size_t kPageTableLevelShift = 3;
constexpr uint32_t kPsciMajorVersion = 0;
constexpr uint32_t kPsciMinorVersion = 2;
constexpr uint16_t kSmcPsci = 0;

enum TimerControl : uint32_t {
  ENABLE = 1u << 0,
  IMASK = 1u << 1,
  ISTATUS = 1u << 2,
};

// Note: This function assumes that the timer being used by the host is the
// virtual view of the ARM system timer, or equivalent (eg. the physical timer
// with CNTVOFF_EL2 set to zero).  This is _currently_ true, as the Fuchsia EL2
// code seems to always set CNTVOFF_EL2 offset to zero, and then just leave it
// there for all time.  If this ever changes, this code will need to be updated
// to account for the difference between the physical and virtual views of the
// system timer.
zx_ticks_t convert_raw_ticks_to_ticks(zx_ticks_t raw_ticks) {
  return raw_ticks + platform_get_raw_ticks_to_ticks_offset();
}

void next_pc(GuestState* guest_state) { guest_state->system_state.elr_el2 += 4; }

bool timer_enabled(GuestState* guest_state) {
  bool enabled = guest_state->cntv_ctl_el0 & TimerControl::ENABLE;
  bool masked = guest_state->cntv_ctl_el0 & TimerControl::IMASK;
  return enabled && !masked;
}

zx_status_t handle_wfi_wfe_instruction(uint32_t iss, GuestState* guest_state,
                                       GichState* gich_state) {
  next_pc(guest_state);
  const WaitInstruction wi(iss);
  if (wi.is_wfe) {
    ktrace_vcpu_exit(VCPU_WFE_INSTRUCTION, guest_state->system_state.elr_el2);
    return ZX_OK;
  }
  ktrace_vcpu_exit(VCPU_WFI_INSTRUCTION, guest_state->system_state.elr_el2);

  // If a list register is in use, then we have an active interrupt.
  if (gich_state->IsUsingListRegister()) {
    return ZX_OK;
  }

  zx_time_t deadline = ZX_TIME_INFINITE;
  if (timer_enabled(guest_state)) {
    zx_ticks_t guest_ticks_deadline = convert_raw_ticks_to_ticks(guest_state->cntv_cval_el0);
    if (current_ticks() >= guest_ticks_deadline) {
      return ZX_OK;
    }
    deadline = platform_get_ticks_to_time_ratio().Scale(guest_ticks_deadline);
  }
  return gich_state->Wait(deadline);
}

zx_status_t handle_smc_instruction(uint32_t iss, GuestState* guest_state,
                                   zx_port_packet_t* packet) {
  const SmcInstruction si(iss);
  if (si.imm != kSmcPsci) {
    dprintf(CRITICAL, "hypervisor: Unhandled guest SMC instruction %#lx\n", guest_state->x[0]);
    // From ARM DEN 0028B, Section 5.2: The Unknown SMC Function Identifier is a sign-extended
    // value of (-1) that is returned in R0, W0 or X0 register.
    guest_state->x[0] = ~0ul;
    next_pc(guest_state);
    return ZX_OK;
  }

  next_pc(guest_state);
  switch (guest_state->x[0]) {
    case PSCI64_PSCI_VERSION:
      // See ARM PSCI Platform Design Document, Section 5.1.1.
      guest_state->x[0] = (kPsciMajorVersion << 16) | kPsciMinorVersion;
      return ZX_OK;
    case PSCI64_CPU_ON:
      memset(packet, 0, sizeof(*packet));
      packet->type = ZX_PKT_TYPE_GUEST_VCPU;
      packet->guest_vcpu.type = ZX_PKT_GUEST_VCPU_STARTUP;
      packet->guest_vcpu.startup.id = guest_state->x[1];
      packet->guest_vcpu.startup.entry = guest_state->x[2];
      guest_state->x[0] = PSCI_SUCCESS;
      return ZX_ERR_NEXT;
    case PSCI64_CPU_OFF:
      return ZX_ERR_STOP;
    case PSCI64_SYSTEM_OFF:
      return ZX_ERR_UNAVAILABLE;
    case PSCI64_SYSTEM_RESET:
      // See ARM PSCI Platform Design Document, Section 5.11.
      return ZX_ERR_CANCELED;
    default:
      dprintf(CRITICAL, "hypervisor: Unhandled guest SMC PSCI instruction %#lx\n",
              guest_state->x[0]);
      guest_state->x[0] = PSCI_NOT_SUPPORTED;
      return ZX_OK;
  }
}

void clean_invalidate_cache(zx_paddr_t table, size_t index_shift) {
  // TODO(abdulla): Make this understand concatenated page tables.
  auto* pte = static_cast<pte_t*>(paddr_to_physmap(table));
  pte_t page = index_shift > MMU_GUEST_PAGE_SIZE_SHIFT ? MMU_PTE_L012_DESCRIPTOR_BLOCK
                                                       : MMU_PTE_L3_DESCRIPTOR_PAGE;
  for (size_t i = 0; i < PAGE_SIZE / sizeof(pte_t); i++) {
    pte_t desc = pte[i] & MMU_PTE_DESCRIPTOR_MASK;
    pte_t paddr = pte[i] & MMU_PTE_OUTPUT_ADDR_MASK;
    if (desc == page) {
      zx_vaddr_t vaddr = reinterpret_cast<zx_vaddr_t>(paddr_to_physmap(paddr));
      arch_clean_invalidate_cache_range(vaddr, 1lu << index_shift);
    } else if (desc != MMU_PTE_DESCRIPTOR_INVALID) {
      size_t adjust_shift = MMU_GUEST_PAGE_SIZE_SHIFT - kPageTableLevelShift;
      clean_invalidate_cache(paddr, index_shift - adjust_shift);
    }
  }

  // Invalidate guest i-cache,
  arch::InvalidateGlobalInstructionCache();
}

zx_status_t handle_system_instruction(uint32_t iss, uint64_t& hcr, GuestState* guest_state,
                                      hypervisor::GuestPhysicalAspace* gpa,
                                      zx_port_packet_t* packet) {
  const SystemInstruction si(iss);
  const uint64_t reg = guest_state->x[si.xt];

  switch (si.sysreg) {
    case SystemRegister::MAIR_EL1:
      return SET_SYSREG(mair_el1);
    case SystemRegister::SCTLR_EL1: {
      if (si.read) {
        return ZX_ERR_NOT_SUPPORTED;
      }

      uint32_t sctlr_el1 = reg & UINT32_MAX;

      // If the MMU is being enabled and caches are on, invalidate the caches.
      //
      // At this point the guest may reasonably assume that the caches are
      // clear, but accesses by the host (either directly or even by just
      // a speculative CPU load) may have led to them containing data. If this
      // has happened, a guest's write to raw memory may be hidden by a stale
      // cache entry.
      //
      // Invalidating the caches removes all stale data from cache. It's not
      // a problem if a cache line is brought back into the cache after we
      // invalidate: it will correctly contain the guest's data.
      bool mmu_enabled = (sctlr_el1 & SCTLR_ELX_M) != 0;
      bool dcaches_enabled = (sctlr_el1 & SCTLR_ELX_C) != 0;
      if (mmu_enabled && dcaches_enabled) {
        // Clean/invalidate the pages. We don't strictly need the clean, but it
        // doesn't hurt.
        clean_invalidate_cache(gpa->arch_aspace().arch_table_phys(), MMU_GUEST_TOP_SHIFT);

        // Stop trapping MMU register accesses to improve performance.
        //
        // We'll start monitoring again if the guest does a set/way cache
        // operation.
        hcr &= ~HCR_EL2_TVM;
      }

      LTRACEF("guest sctlr_el1: %#x\n", sctlr_el1);
      LTRACEF("guest hcr_el2: %#lx\n", hcr);

      guest_state->system_state.sctlr_el1 = sctlr_el1;
      next_pc(guest_state);
      return ZX_OK;
    }
    case SystemRegister::TCR_EL1:
      return SET_SYSREG(tcr_el1);
    case SystemRegister::TTBR0_EL1:
      return SET_SYSREG(ttbr0_el1);
    case SystemRegister::TTBR1_EL1:
      return SET_SYSREG(ttbr1_el1);
    case SystemRegister::OSLAR_EL1:
    case SystemRegister::OSLSR_EL1:
    case SystemRegister::OSDLR_EL1:
    case SystemRegister::DBGPRCR_EL1:
      next_pc(guest_state);
      // These registers are RAZ/WI. Their state is dictated by the host.
      if (si.read) {
        guest_state->x[si.xt] = 0;
      }
      return ZX_OK;
    case SystemRegister::ICC_SGI1R_EL1: {
      if (si.read) {
        // ICC_SGI1R_EL1 is write-only.
        return ZX_ERR_INVALID_ARGS;
      }
      SgiRegister sgi(reg);
      if (sgi.aff3 != 0 || sgi.aff2 != 0 || sgi.aff1 != 0 || sgi.rs != 0) {
        return ZX_ERR_NOT_SUPPORTED;
      }

      memset(packet, 0, sizeof(*packet));
      packet->type = ZX_PKT_TYPE_GUEST_VCPU;
      packet->guest_vcpu.type = ZX_PKT_GUEST_VCPU_INTERRUPT;
      if (sgi.all_but_local) {
        auto vpid = BITS(guest_state->vmpidr_el2, 16, 0);
        packet->guest_vcpu.interrupt.mask = ~(1ul << vpid);
      } else {
        packet->guest_vcpu.interrupt.mask = sgi.target_list;
      }
      packet->guest_vcpu.interrupt.vector = sgi.int_id;
      next_pc(guest_state);
      return ZX_ERR_NEXT;
    }
    case SystemRegister::DC_ISW:
    case SystemRegister::DC_CISW:
    case SystemRegister::DC_CSW: {
      // Clean and invalidate the cache.
      //
      // The guest will typically need to iterate over a large number of
      // sets/ways to do a full clean/invalidate. To avoid doing several full
      // cache cleans in a row, we only do a cache operation when the guest is
      // operating on set/way 0.
      //
      // The guest can't know the mapping between set/way and physical memory,
      // so are required to iterate through every set/way. If the guest
      // doesn't do this, they shouldn't be surprised if not everything has
      // been cleaned.
      uint64_t set_way = BITS_SHIFT(reg, 31, 4);
      if (set_way == 0) {
        clean_invalidate_cache(gpa->arch_aspace().arch_table_phys(), MMU_GUEST_TOP_SHIFT);
      }

      // If the MMU or caches are off, start monitoring guest SCTLR register
      // accesses so we can determine when the MMU/caches are turned on again.
      //
      // When the MMU or caches are turned off and the guest has just cleared
      // caches, the guest can reasonably assume that the caches will remain
      // clear, and that they won't need to invalidate them again prior to the
      // MMU being turned on again.
      //
      // We (the host) can't guarantee that the we won't inadvertently cause
      // the cache lines to load again (e.g., through speculative CPU
      // accesses). Instead, we start monitoring for when the guest turns on
      // the MMU again, and clean/invalidate caches then. This ensures that
      // any writes done by the guest while caches are disabled won't be
      // hidden by stale cache lines.
      uint32_t sctlr_el1 = guest_state->system_state.sctlr_el1;
      bool mmu_enabled = (sctlr_el1 & SCTLR_ELX_M) != 0;
      bool dcaches_enabled = (sctlr_el1 & SCTLR_ELX_C) != 0;
      if (!mmu_enabled || !dcaches_enabled) {
        hcr |= HCR_EL2_TVM;
      }

      next_pc(guest_state);
      return ZX_OK;
    }
    default:
      dprintf(CRITICAL, "hypervisor: Unhandled guest system register %#x access\n",
              static_cast<uint16_t>(si.sysreg));
      return ZX_ERR_NOT_SUPPORTED;
  }
}

zx_status_t handle_instruction_abort(GuestState* guest_state,
                                     hypervisor::GuestPhysicalAspace* gpa) {
  const zx_vaddr_t guest_paddr = guest_state->hpfar_el2;
  if (auto result = gpa->PageFault(guest_paddr); result.is_error()) {
    dprintf(CRITICAL, "hypervisor: Unhandled guest instruction abort %#lx\n", guest_paddr);
    return result.status_value();
  }
  return ZX_OK;
}

zx_status_t handle_data_abort(uint32_t iss, GuestState* guest_state,
                              hypervisor::GuestPhysicalAspace* gpa, hypervisor::TrapMap* traps,
                              zx_port_packet_t* packet) {
  zx_vaddr_t guest_paddr = guest_state->hpfar_el2;
  zx::status<hypervisor::Trap*> trap = traps->FindTrap(ZX_GUEST_TRAP_BELL, guest_paddr);
  switch (trap.status_value()) {
    case ZX_ERR_NOT_FOUND:
      if (auto result = gpa->PageFault(guest_paddr); result.is_error()) {
        dprintf(CRITICAL, "hypervisor: Unhandled guest data abort %#lx\n", guest_paddr);
        return result.status_value();
      }
      return ZX_OK;
    case ZX_OK:
      break;
    default:
      return trap.status_value();
  }
  next_pc(guest_state);

  // Combine the lower bits of FAR_EL2 with HPFAR_EL2 to get the exact IPA.
  guest_paddr |= guest_state->far_el2 & (PAGE_SIZE - 1);
  LTRACEF("guest far_el2: %#lx\n", guest_state->far_el2);

  const DataAbort data_abort(iss);
  switch ((*trap)->kind()) {
    case ZX_GUEST_TRAP_BELL:
      if (data_abort.read) {
        return ZX_ERR_NOT_SUPPORTED;
      }
      packet->key = (*trap)->key();
      packet->type = ZX_PKT_TYPE_GUEST_BELL;
      packet->guest_bell.addr = guest_paddr;
      if (!(*trap)->HasPort()) {
        return ZX_ERR_BAD_STATE;
      }
      return (*trap)->Queue(*packet).status_value();
    case ZX_GUEST_TRAP_MEM:
      if (!data_abort.valid) {
        return ZX_ERR_IO_DATA_INTEGRITY;
      }
      packet->key = (*trap)->key();
      packet->type = ZX_PKT_TYPE_GUEST_MEM;
      packet->guest_mem.addr = guest_paddr;
      packet->guest_mem.access_size = data_abort.access_size;
      packet->guest_mem.sign_extend = data_abort.sign_extend;
      packet->guest_mem.xt = data_abort.xt;
      packet->guest_mem.read = data_abort.read;
      if (!data_abort.read) {
        packet->guest_mem.data = guest_state->x[data_abort.xt];
      }
      return ZX_ERR_NEXT;
    default:
      return ZX_ERR_BAD_STATE;
  }
}

std::string_view ErrorTypeToString(SError::ErrorType type) {
  switch (type) {
    case SError::ErrorType::kUncontainable:
      return "Uncontainable";
    case SError::ErrorType::kUnrecoverableState:
      return "Unrecoverable State";
    case SError::ErrorType::kRestartableState:
      return "Restartable State";
    case SError::ErrorType::kRecoverableState:
      return "Recoverable State";
    case SError::ErrorType::kCorrected:
      return "Corrected";
    default:
      return "Unknown";
  }
}

std::string_view DataFaultStatusCodeToString(SError::DataFaultStatusCode code) {
  switch (code) {
    case SError::DataFaultStatusCode::kUncategorized:
      return "Uncategorized";
    case SError::DataFaultStatusCode::kAsyncSError:
      return "Async SError";
    default:
      return "Unknown";
  }
}

zx_status_t handle_serror_interrupt(GuestState* guest_state, uint32_t iss) {
  // We received a system error (SError) exception.
  //
  // This isn't necessarily the guest's fault. It might be the host (kernel or
  // userspace) triggered the SError, but it wasn't reported until the guest
  // happened to be running.
  //
  // Print out a log and continue.
  const SError serror(iss);
  std::string_view aet_string = ErrorTypeToString(serror.aet());
  std::string_view dfsc_string = DataFaultStatusCodeToString(serror.dfsc());
  dprintf(CRITICAL,
          "hypervisor: Received SError while running guest. Ignoring. "
          "(Guest at EL%u, PC=%#lx. "
          "CPU: %u, Syndrome: ISS=%#x [IDS=%u; IESB=%u; AET=%#x (%*s); EA=%u; DFSC=%#x (%*s)])\n",
          guest_state->el(), guest_state->system_state.elr_el2, arch_curr_cpu_num(), serror.iss,
          serror.ids(), serror.iesb(), static_cast<uint32_t>(serror.aet()),
          static_cast<int>(aet_string.size()), aet_string.data(), serror.ea(),
          static_cast<uint32_t>(serror.dfsc()), static_cast<int>(dfsc_string.size()),
          dfsc_string.data());
  return ZX_OK;
}

}  // namespace

ExceptionSyndrome::ExceptionSyndrome(uint32_t esr) {
  ec = static_cast<ExceptionClass>(BITS_SHIFT(esr, 31, 26));
  iss = BITS(esr, 24, 0);
}

WaitInstruction::WaitInstruction(uint32_t iss) { is_wfe = BIT(iss, 0); }

SmcInstruction::SmcInstruction(uint32_t iss) { imm = static_cast<uint16_t>(BITS(iss, 15, 0)); }

SystemInstruction::SystemInstruction(uint32_t iss) {
  sysreg = static_cast<SystemRegister>(BITS(iss, 21, 10) >> 6 | BITS_SHIFT(iss, 4, 1));
  xt = static_cast<uint8_t>(BITS_SHIFT(iss, 9, 5));
  read = BIT(iss, 0);
}

SgiRegister::SgiRegister(uint64_t sgir) {
  aff3 = static_cast<uint8_t>(BITS_SHIFT(sgir, 55, 48));
  aff2 = static_cast<uint8_t>(BITS_SHIFT(sgir, 39, 32));
  aff1 = static_cast<uint8_t>(BITS_SHIFT(sgir, 23, 16));
  rs = static_cast<uint8_t>(BITS_SHIFT(sgir, 47, 44));
  target_list = static_cast<uint8_t>(BITS_SHIFT(sgir, 15, 0));
  int_id = static_cast<uint8_t>(BITS_SHIFT(sgir, 27, 24));
  all_but_local = BIT(sgir, 40);
}

DataAbort::DataAbort(uint32_t iss) {
  valid = BIT_SHIFT(iss, 24);
  access_size = static_cast<uint8_t>(1u << BITS_SHIFT(iss, 23, 22));
  sign_extend = BIT(iss, 21);
  xt = static_cast<uint8_t>(BITS_SHIFT(iss, 20, 16));
  read = !BIT(iss, 6);
}

void timer_maybe_interrupt(GuestState* guest_state, GichState* gich_state) {
  if (timer_enabled(guest_state)) {
    zx_ticks_t guest_ticks_deadline = convert_raw_ticks_to_ticks(guest_state->cntv_cval_el0);
    if (current_ticks() >= guest_ticks_deadline) {
      gich_state->Track(kTimerVector);
    }
  }
}

zx_status_t vmexit_handler(uint64_t* hcr, GuestState* guest_state, GichState* gich_state,
                           hypervisor::GuestPhysicalAspace* gpa, hypervisor::TrapMap* traps,
                           zx_port_packet_t* packet) {
  LTRACEF("guest esr_el1: %#x\n", guest_state->system_state.esr_el1);
  LTRACEF("guest esr_el2: %#x\n", guest_state->esr_el2);
  LTRACEF("guest elr_el2: %#lx\n", guest_state->system_state.elr_el2);
  LTRACEF("guest spsr_el2: %#x\n", guest_state->system_state.spsr_el2);

  ExceptionSyndrome syndrome(guest_state->esr_el2);
  zx_status_t status;
  switch (syndrome.ec) {
    case ExceptionClass::WFI_WFE_INSTRUCTION:
      LTRACEF("handling wfi/wfe instruction, iss %#x\n", syndrome.iss);
      GUEST_STATS_INC(wfi_wfe_instructions);
      status = handle_wfi_wfe_instruction(syndrome.iss, guest_state, gich_state);
      break;
    case ExceptionClass::SMC_INSTRUCTION:
      LTRACEF("handling smc instruction, iss %#x func %#lx\n", syndrome.iss, guest_state->x[0]);
      GUEST_STATS_INC(smc_instructions);
      ktrace_vcpu_exit(VCPU_SMC_INSTRUCTION, guest_state->system_state.elr_el2);
      status = handle_smc_instruction(syndrome.iss, guest_state, packet);
      break;
    case ExceptionClass::SYSTEM_INSTRUCTION:
      LTRACEF("handling system instruction\n");
      GUEST_STATS_INC(system_instructions);
      ktrace_vcpu_exit(VCPU_SYSTEM_INSTRUCTION, guest_state->system_state.elr_el2);
      status = handle_system_instruction(syndrome.iss, *hcr, guest_state, gpa, packet);
      break;
    case ExceptionClass::INSTRUCTION_ABORT:
      LTRACEF("handling instruction abort at %#lx\n", guest_state->hpfar_el2);
      GUEST_STATS_INC(instruction_aborts);
      ktrace_vcpu_exit(VCPU_INSTRUCTION_ABORT, guest_state->system_state.elr_el2);
      status = handle_instruction_abort(guest_state, gpa);
      break;
    case ExceptionClass::DATA_ABORT:
      LTRACEF("handling data abort at %#lx\n", guest_state->hpfar_el2);
      GUEST_STATS_INC(data_aborts);
      ktrace_vcpu_exit(VCPU_DATA_ABORT, guest_state->system_state.elr_el2);
      status = handle_data_abort(syndrome.iss, guest_state, gpa, traps, packet);
      break;
    case ExceptionClass::SERROR_INTERRUPT:
      LTRACEF("handling serror interrupt at %#lx\n", guest_state->hpfar_el2);
      ktrace_vcpu_exit(VCPU_SERROR_INTERRUPT, guest_state->system_state.elr_el2);
      status = handle_serror_interrupt(guest_state, syndrome.iss);
      break;
    default:
      LTRACEF("unhandled exception syndrome, ec %#x iss %#x\n", static_cast<uint32_t>(syndrome.ec),
              syndrome.iss);
      ktrace_vcpu_exit(VCPU_NOT_SUPPORTED, guest_state->system_state.elr_el2);
      status = ZX_ERR_NOT_SUPPORTED;
      break;
  }
  switch (status) {
    case ZX_OK:
    case ZX_ERR_NEXT:
    case ZX_ERR_STOP:
    case ZX_ERR_UNAVAILABLE:
    case ZX_ERR_INTERNAL_INTR_RETRY:
    case ZX_ERR_INTERNAL_INTR_KILLED:
      break;
    default:
      dprintf(CRITICAL, "hypervisor: VM exit handler for %u (%s) in EL%u at %#lx returned %d\n",
              static_cast<uint32_t>(syndrome.ec), exception_class_name(syndrome.ec),
              guest_state->el(), guest_state->system_state.elr_el2, status);
      break;
  }
  return status;
}
