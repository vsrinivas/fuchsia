// Copyright 2017 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <bits.h>
#include <platform.h>
#include <trace.h>

#include <arch/arch_ops.h>
#include <arch/mp.h>
#include <arch/x86/acpi.h>
#include <arch/x86/bootstrap16.h>
#include <arch/x86/feature.h>
#include <arch/x86/platform_access.h>
#include <fbl/auto_call.h>
#include <kernel/percpu.h>
#include <kernel/timer.h>
#include <vm/vm_aspace.h>

#include "system_priv.h"
extern "C" {
#include <acpica/accommon.h>
#include <acpica/achware.h>
#include <acpica/acpi.h>
}

#include <pow2.h>

#define LOCAL_TRACE 0

#define MAX_LONG_TERM_POWER_LIMIT 0x7FFF

namespace {

// This thread performs the work for suspend/resume.  We use a separate thread
// rather than the invoking thread to let us lean on the context switch code
// path to persist all of the usermode thread state that is not saved on a plain
// mode switch.
zx_status_t suspend_thread(void* raw_arg) {
  auto arg = reinterpret_cast<const zx_system_powerctl_arg_t*>(raw_arg);
  uint8_t target_s_state = arg->acpi_transition_s_state.target_s_state;
  uint8_t sleep_type_a = arg->acpi_transition_s_state.sleep_type_a;
  uint8_t sleep_type_b = arg->acpi_transition_s_state.sleep_type_b;

  // Acquire resources for suspend and resume if necessary.
  fbl::RefPtr<VmAspace> temp_aspace;
  x86_realmode_entry_data* bootstrap_data;
  struct x86_realmode_entry_data_registers regs;
  paddr_t bootstrap_ip;
  zx_status_t status;
  status = x86_bootstrap16_acquire(reinterpret_cast<uintptr_t>(_x86_suspend_wakeup), &temp_aspace,
                                   reinterpret_cast<void**>(&bootstrap_data), &bootstrap_ip);
  if (status != ZX_OK) {
    return status;
  }
  auto bootstrap_cleanup =
      fbl::MakeAutoCall([&bootstrap_data]() { x86_bootstrap16_release(bootstrap_data); });

  // Setup our resume path
  ACPI_TABLE_FACS* facs = nullptr;
  ACPI_STATUS acpi_status =
      AcpiGetTable((char*)ACPI_SIG_FACS, 1, reinterpret_cast<ACPI_TABLE_HEADER**>(&facs));
  if (acpi_status != AE_OK) {
    return ZX_ERR_BAD_STATE;
  }
  acpi_status = AcpiHwSetFirmwareWakingVector(facs, bootstrap_ip, 0);
  if (acpi_status != AE_OK) {
    return ZX_ERR_BAD_STATE;
  }
  auto wake_vector_cleanup =
      fbl::MakeAutoCall([facs]() { AcpiHwSetFirmwareWakingVector(facs, 0, 0); });

  bootstrap_data->registers_ptr = reinterpret_cast<uintptr_t>(&regs);

  interrupt_saved_state_t int_state = arch_interrupt_save();

  // Save system state.
  platform_suspend();
  arch_suspend();

  // Do the actual suspend
  TRACEF("Entering x86_acpi_transition_s_state\n");
  acpi_status = x86_acpi_transition_s_state(&regs, target_s_state, sleep_type_a, sleep_type_b);
  if (acpi_status != AE_OK) {
    arch_interrupt_restore(int_state);
    TRACEF("x86_acpi_transition_s_state failed: %x\n", acpi_status);
    return ZX_ERR_INTERNAL;
  }
  TRACEF("Left x86_acpi_transition_s_state\n");

  // If we're here, we've resumed and need to restore our CPU context
  DEBUG_ASSERT(arch_ints_disabled());

  arch_resume();
  platform_resume();
  percpu::Get(arch_curr_cpu_num()).timer_queue.ThawPercpu();

  DEBUG_ASSERT(arch_ints_disabled());
  arch_interrupt_restore(int_state);
  return ZX_OK;
}

zx_status_t x86_set_pkg_pl1(const zx_system_powerctl_arg_t* arg, MsrAccess* msr) {
  auto x86_microarch = x86_get_microarch_config()->x86_microarch;
  if ((x86_microarch != X86_MICROARCH_INTEL_SANDY_BRIDGE) &&
      (x86_microarch != X86_MICROARCH_INTEL_SILVERMONT) &&
      (x86_microarch != X86_MICROARCH_INTEL_BROADWELL) &&
      (x86_microarch != X86_MICROARCH_INTEL_HASWELL) &&
      (x86_microarch != X86_MICROARCH_INTEL_SKYLAKE)) {
    return ZX_ERR_NOT_SUPPORTED;
  }

  uint32_t power_limit = arg->x86_power_limit.power_limit;
  uint32_t time_window = arg->x86_power_limit.time_window;
  uint8_t clamp = arg->x86_power_limit.clamp;
  uint8_t enable = arg->x86_power_limit.enable;

  // MSR_RAPL_POWER_UNIT provides the following information across all
  // RAPL domains
  // Power Units[3:0]: power info (in watts) is based on the multiplier,
  // 1/2^PU where PU is an unsigned integer represented by bits [3:0]
  // Time Units[19:16]: Time info (in seconds) is based on multiplier,
  // 1/2^TU where TU is an uint represented by bits[19:16]
  // Based on Intel Software Manual vol 3, chapter 14.9

  uint64_t rapl_unit = msr->read_msr(X86_MSR_RAPL_POWER_UNIT);

  // zx_system_powerctl_arg_t is in mW and us, hence the math below
  // power unit is represented in bits [3:0] in the RAPL_POWER_UNIT MSR
  // time unit is represented in bits [19:16] in RAPL_POWER_UNIT MSR
  uint32_t power_units = 1000 / (1 << BITS_SHIFT(rapl_unit, 3, 0));
  uint32_t time_units = 1000000 / (1 << BITS_SHIFT(rapl_unit, 19, 16));

  // MSR_PKG_POWER_LIMIT allows SW to define power limit from package domain
  // power limit is defined in terms of avg power over a time window
  // Power limit 1[14:0]: sets avg power limit of package domain corresponding
  // to time window 1. Unit is in MSR_RAPL_POWER_UNIT
  // Enable power limit[15]: 0-disabled, 1-enabled
  // Package clamp limit1[16]: Allow going below OS requested p/t states
  // Time window[23:17]: Time limit = 2^Y * (1.0 + Z/4.0) * Time_Unit
  // Y = uint in bits[21:17] and Z = uint in bits[23:22]
  // Based on Intel Software Manual vol 3, chapter 14.9

  uint64_t rapl = msr->read_msr(X86_MSR_PKG_POWER_LIMIT);

  rapl &= ~BITMAP_LAST_WORD_MASK(15);

  if (power_limit > 0) {
    uint64_t raw_msr = power_limit / power_units;
    if (raw_msr > MAX_LONG_TERM_POWER_LIMIT) {
      return ZX_ERR_INVALID_ARGS;
    }

    rapl |= BITS(raw_msr, 15, 0);
  } else {
    // MSR_PKG_POWER_INFO is a RO MSR that reports package power range for RAPL
    // Thermal Spec power[14:0]: The value here is the equivalent of thermal spec power
    // of package domain. Setting to this thermal spec power if input is 0
    rapl |= BITS_SHIFT(msr->read_msr(X86_MSR_PKG_POWER_INFO), 15, 0);
  }

  // Based on Intel Software Manual vol 3, chapter 14.9,
  // Time limit = 2^Y * (1.0 + Z/4.0) * Time_Unit

  rapl &= ~0xFE0000;

  if (time_window > 0) {
    uint64_t t = time_window / time_units;
    uint64_t y = log2_ulong_floor(t);
    uint64_t z = (((4 * t)) / (1 << y)) - 4;
    t = (y & 0x1F) | ((z & 0x3) << 5);
    rapl |= t << 17;
  } else {
    // set to default
    uint64_t t = (msr->read_msr(X86_MSR_PKG_POWER_INFO) >> 17) & 0x007F;
    rapl |= t << 17;
  }
  if (clamp) {
    rapl |= X86_MSR_PKG_POWER_LIMIT_PL1_CLAMP;
  } else {
    rapl &= ~X86_MSR_PKG_POWER_LIMIT_PL1_CLAMP;
  }

  if (enable) {
    rapl |= X86_MSR_PKG_POWER_LIMIT_PL1_ENABLE;
  } else {
    rapl &= ~X86_MSR_PKG_POWER_LIMIT_PL1_ENABLE;
  }

  msr->write_msr(X86_MSR_PKG_POWER_LIMIT, rapl);
  return ZX_OK;
}

zx_status_t acpi_transition_s_state(const zx_system_powerctl_arg_t* arg) {
  uint8_t target_s_state = arg->acpi_transition_s_state.target_s_state;
  uint8_t sleep_type_a = arg->acpi_transition_s_state.sleep_type_a;
  uint8_t sleep_type_b = arg->acpi_transition_s_state.sleep_type_b;
  if (target_s_state == 0 || target_s_state > 5) {
    TRACEF("Bad S-state: S%u\n", target_s_state);
    return ZX_ERR_INVALID_ARGS;
  }

  // If not a shutdown, ensure CPU 0 is the only cpu left running.
  if (target_s_state != 5 && mp_get_online_mask() != cpu_num_to_mask(0)) {
    TRACEF("Too many CPUs running for state S%u\n", target_s_state);
    return ZX_ERR_BAD_STATE;
  }

  // Acquire resources for suspend and resume if necessary.
  if (target_s_state < 5) {
    // If we're not shutting down, prepare a resume path and execute the
    // suspend on a separate thread (see comment on |suspend_thread()| for
    // explanation).
    Thread* t = Thread::Create("suspend-thread", suspend_thread,
                               const_cast<zx_system_powerctl_arg_t*>(arg), HIGHEST_PRIORITY);
    if (!t) {
      return ZX_ERR_NO_MEMORY;
    }

    t->Resume();

    zx_status_t retcode;
    zx_status_t status = t->Join(&retcode, ZX_TIME_INFINITE);
    ASSERT(status == ZX_OK);

    if (retcode != ZX_OK) {
      return retcode;
    }
  } else {
    struct x86_realmode_entry_data_registers regs;

    DEBUG_ASSERT(target_s_state == 5);
    interrupt_saved_state_t int_state = arch_interrupt_save();

    ACPI_STATUS acpi_status =
        x86_acpi_transition_s_state(&regs, target_s_state, sleep_type_a, sleep_type_b);
    arch_interrupt_restore(int_state);
    if (acpi_status != AE_OK) {
      return ZX_ERR_INTERNAL;
    }
  }

  return ZX_OK;
}

}  // namespace

zx_status_t arch_system_powerctl(uint32_t cmd, const zx_system_powerctl_arg_t* arg,
                                 MsrAccess* msr) {
  switch (cmd) {
    case ZX_SYSTEM_POWERCTL_ACPI_TRANSITION_S_STATE:
      return acpi_transition_s_state(arg);
    case ZX_SYSTEM_POWERCTL_X86_SET_PKG_PL1:
      return x86_set_pkg_pl1(arg, msr);
    default:
      return ZX_ERR_NOT_SUPPORTED;
  }
}
