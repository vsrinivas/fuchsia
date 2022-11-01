// Copyright 2021 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <lib/arch/arm64/system.h>
#include <lib/zbitl/view.h>
#include <stdio.h>
#include <zircon/boot/driver-config.h>
#include <zircon/boot/image.h>

#include <phys/exception.h>
#include <phys/main.h>
#include <phys/stack.h>

#include "psci.h"

// The regs.h header is really only used by assembly code.
// This is just here to get the static_asserts done somewhere.
#include "regs.h"

namespace {

// The vector table is defined in assembly (see exception.S).
extern "C" const uint32_t phys_exception[];

template <typename Elr, typename Spsr, typename Sp>
struct ResumeRegs {
  void operator()(PhysExceptionState& state, uint64_t pc, uint64_t sp, uint64_t psr) {
    // The SPSR_ELx and ELR_ELx for the current EL are restored by ERET.
    elr_.set_pc(pc);
    elr_.Write();
    spsr_.set_reg_value(psr);
    spsr_.Write();

    // The SP is more complicated.  See below.
    sp_(spsr_, sp);
  }

  Elr elr_;
  Spsr spsr_;
  Sp sp_;
};

template <typename Elr, typename Spsr, typename Sp>
ResumeRegs(Elr&&, Spsr&&, Sp&&) -> ResumeRegs<Elr, Spsr, Sp>;

// We assume SPSel is set in the current EL, so the SP that exception.S will
// restore will be SP_ELx.  If we're returning to the same EL, there is nothing
// more to do.  If we're returning to a lower EL, we have to set either SP_ELx
// for that EL or SP_EL0 depending on the SPSel bit being restored.

struct SpSameEl {
  void set_sp(uint64_t pc) {}
  void Write() {}
};

struct SpBadEl {
  void set_sp(uint64_t pc) {}
  void Write() { ZX_PANIC("cannot return to a higher EL!"); }
};

template <class El1, class El2, class El3>
constexpr auto ResumeSpElx = [](auto&& spsr, uint64_t sp) {
  auto write = [sp](auto&& sp_elx) {
    sp_elx.set_sp(sp);
    sp_elx.Write();
  };
  if (spsr.spsel()) {
    spsr.el().ForThisEl(El1(), El2(), El3(), write);
  } else {
    write(arch::ArmSpEl0());
  }
};

constexpr auto ResumeSpEl1 = ResumeSpElx<SpSameEl, SpBadEl, SpBadEl>;
constexpr auto ResumeSpEl2 = ResumeSpElx<arch::ArmSpEl1, SpSameEl, SpBadEl>;
constexpr auto ResumeSpEl3 = ResumeSpElx<arch::ArmSpEl1, arch::ArmSpEl2, SpSameEl>;

const zbi_dcfg_arm_psci_driver_t* FindPsciConfig(void* zbi_ptr) {
  const zbi_dcfg_arm_psci_driver_t* cfg = nullptr;

  zbitl::View zbi(zbitl::StorageFromRawHeader(static_cast<zbi_header_t*>(zbi_ptr)));
  for (auto [header, payload] : zbi) {
    if (header->type == ZBI_TYPE_KERNEL_DRIVER && header->extra == ZBI_KERNEL_DRIVER_ARM_PSCI &&
        payload.size() >= sizeof(*cfg)) {
      // Keep looping.  The Last one wins.
      cfg = reinterpret_cast<const zbi_dcfg_arm_psci_driver_t*>(payload.data());
    }
  }
  zbi.ignore_error();

  return cfg;
}

}  // namespace

void ArchSetUp(void* zbi) {
  // Hereafter any machine exceptions should be handled.
  ArmSetVbar(phys_exception);

  ArmPsciSetup(FindPsciConfig(zbi));
}

uint64_t PhysExceptionResume(PhysExceptionState& state, uint64_t pc, uint64_t sp, uint64_t psr) {
  // Update the fields in the trap frame just for consistency.  The PC and SPSR
  // here are never used by the hardware, but the SP is used sometimes.
  state.regs.pc = pc;
  state.regs.sp = sp;
  state.regs.cpsr = psr;

  arch::ArmCurrentEl::Read().ForThisEl(
      ResumeRegs{arch::ArmElrEl1(), arch::ArmSpsrEl1(), ResumeSpEl1},
      ResumeRegs{arch::ArmElrEl2(), arch::ArmSpsrEl2(), ResumeSpEl2},
      ResumeRegs{arch::ArmElrEl3(), arch::ArmSpsrEl3(), ResumeSpEl3},
      // Set the CPU values to match what's now in the struct.
      [&](auto&& resume_regs) { resume_regs(state, pc, sp, psr); });

  return PHYS_EXCEPTION_RESUME;
}
