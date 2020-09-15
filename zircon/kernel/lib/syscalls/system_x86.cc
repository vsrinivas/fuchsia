// Copyright 2017 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <bits.h>
#include <platform.h>
#include <pow2.h>
#include <trace.h>

#include <arch/arch_ops.h>
#include <arch/mp.h>
#include <arch/x86/feature.h>
#include <arch/x86/platform_access.h>
#include <kernel/percpu.h>
#include <kernel/timer.h>

#include "system_priv.h"

#define LOCAL_TRACE 0

#define MAX_LONG_TERM_POWER_LIMIT 0x7FFF

namespace {

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

}  // namespace

zx_status_t arch_system_powerctl(uint32_t cmd, const zx_system_powerctl_arg_t* arg,
                                 MsrAccess* msr) {
  switch (cmd) {
    case ZX_SYSTEM_POWERCTL_X86_SET_PKG_PL1:
      return x86_set_pkg_pl1(arg, msr);
    default:
      return ZX_ERR_NOT_SUPPORTED;
  }
}
