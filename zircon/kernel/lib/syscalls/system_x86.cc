// Copyright 2017 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <bits.h>
#include <lib/boot-options/boot-options.h>
#include <lib/console.h>
#include <pow2.h>
#include <trace.h>

#include <arch/arch_ops.h>
#include <arch/mp.h>
#include <arch/x86/feature.h>
#include <arch/x86/platform_access.h>
#include <kernel/percpu.h>
#include <kernel/timer.h>
#include <platform/pc/acpi.h>

#include "system_priv.h"

#define LOCAL_TRACE 0

namespace {

static constexpr uint64_t kMaxLongTermPowerLimit = 0x7FFF;

// Intel recommends a time window of 28s, which corresponds to the following value.
static constexpr uint64_t kDefaultTimeWindow = 0x6e;

// Intel Volume 3 Section 14.9.3.
static constexpr uint64_t kPowerLimitPl1Enable = 1ull << 15;
static constexpr uint64_t kPowerLimitPl1Clamp = 1ull << 16;
static constexpr uint64_t kPowerLimitPl2Enable = 1ull << 47;
static constexpr uint64_t kPowerLimitPl2Clamp = 1ull << 48;

// Intel Volume 4 Table 2-39
static constexpr struct {
  uint64_t bit;
  const char* str;
} kLimitReasons[] = {
    {1 << 0, "PROCHOT"},
    {1 << 1, "Thermal event"},
    {1 << 4, "Residency state regulation limit"},
    {1 << 5, "Running average thermal limit"},
    {1 << 6, "Voltage regulator (VR) thermal alert"},
    {1 << 7, "Voltage regulator (VR) thermal design current limit"},
    {1 << 8, "Other"},
    {1 << 10, "Package/platform-Level PL1"},
    {1 << 11, "Package/platform-Level PL2"},
    {1 << 12, "Max turbo limit"},
    {1 << 13, "Turbo transition attenuation"},
};

// Intel Volume 4 Table 2-39 "MSR_GRAPHICS_PERF_LIMIT_REASONS"
static constexpr struct {
  uint64_t bit;
  const char* str;
} kLimitReasonsGfx[] = {
    {1 << 0, "PROCHOT"},
    {1 << 1, "Thermal event"},
    {1 << 5, "Running average thermal limit"},
    {1 << 6, "Voltage regulator (VR) thermal alert"},
    {1 << 7, "Voltage regulator (VR) thermal design current limit"},
    {1 << 8, "Other"},
    {1 << 10, "Package/platform-Level PL1"},
    {1 << 11, "Package/platform-Level PL2"},
    {1 << 12, "Inefficient operation"},
};

static constexpr uint64_t kLimitReasonsLogShift = 16;

struct rapl_units {
  uint32_t power_mw;
  uint32_t time_us;
  uint32_t energy_uj;
};

rapl_units GetUnits(MsrAccess* msr) {
  // MSR_RAPL_POWER_UNIT provides the following information across all RAPL domains
  // Power Units[3:0]: power info (in watts) is based on the multiplier, 1/2^PU where PU is an
  // unsigned integer represented by bits [3:0].
  //
  // Time Units[19:16]: Time info (in seconds) is based on multiplier, 1/2^TU where TU is an
  // unsigned integer represented by bits[19:16]
  //
  // Energy Units[12:8]: Energy related information (in Joules) is based on the multiplier, 1/2^ESU,
  // where ESU is an unsigned integer represented by bits 12:8.
  //
  // Based on Intel Software Manual vol 3, chapter 14.9.
  //
  // To give better precision we specify power in milliwatts, time in microseconds, and energy in
  // microjoules.
  uint64_t rapl_unit = msr->read_msr(X86_MSR_RAPL_POWER_UNIT);
  rapl_units units = {
      .power_mw = 1000u / (1 << BITS_SHIFT(rapl_unit, 3, 0)),
      .time_us = 1000000u / (1 << BITS_SHIFT(rapl_unit, 19, 16)),
      .energy_uj = 1000000u / (1 << BITS_SHIFT(rapl_unit, 12, 8)),
  };
  return units;
}

zx_status_t SetPkgPl1(const zx_system_powerctl_arg_t* arg, MsrAccess* msr) {
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

  // zx_system_powerctl_arg_t is in mW and us, hence the math below
  rapl_units units = GetUnits(msr);

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
    uint64_t raw_msr = power_limit / units.power_mw;
    if (raw_msr > kMaxLongTermPowerLimit) {
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
    uint64_t t = time_window / units.time_us;
    uint64_t y = log2_ulong_floor(t);
    uint64_t z = (((4 * t)) / (1 << y)) - 4;
    t = (y & 0x1F) | ((z & 0x3) << 5);
    rapl |= t << 17;
  } else {
    rapl |= kDefaultTimeWindow << 17;
  }
  if (clamp) {
    rapl |= kPowerLimitPl1Clamp;
  } else {
    rapl &= ~kPowerLimitPl1Clamp;
  }

  if (enable) {
    rapl |= kPowerLimitPl1Enable;
  } else {
    rapl &= ~kPowerLimitPl1Enable;
  }

  msr->write_msr(X86_MSR_PKG_POWER_LIMIT, rapl);
  return ZX_OK;
}

void print_limits() {
  MsrAccess msr;
  rapl_units units = GetUnits(&msr);

  uint64_t rapl = msr.read_msr(X86_MSR_PKG_POWER_LIMIT);

  // Based on Intel Software Manual vol 3, chapter 14.9,
  // Time limit = 2^Y * (1.0 + Z/4.0) * Time_Unit
  auto y = static_cast<uint32_t>(BITS_SHIFT(rapl, 21, 17));
  auto z = static_cast<uint32_t>(BITS_SHIFT(rapl, 23, 22));
  uint32_t time_window = (1 << y) * (4 + z) * units.time_us / 4;
  auto power_limit = static_cast<uint32_t>(BITS_SHIFT(rapl, 14, 0));

  printf("PL1 limit: %umW\n", power_limit * units.power_mw);
  printf("PL1 window: %uus\n", time_window);
  printf("PL1 %sabled, clamping %sabled\n", rapl & kPowerLimitPl1Enable ? "en" : "dis",
         rapl & kPowerLimitPl1Clamp ? "en" : "dis");

  // Repeat for PL2
  y = BITS_SHIFT(rapl, 53, 49);
  z = BITS_SHIFT(rapl, 55, 54);
  time_window = (1 << y) * (4 + z) * units.time_us / 4;
  power_limit = BITS_SHIFT(rapl, 46, 32);

  printf("PL2 limit: %umW\n", power_limit * units.power_mw);
  printf("PL2 window: %uus\n", time_window);
  printf("PL2 %sabled, clamping %sabled\n", rapl & kPowerLimitPl2Enable ? "en" : "dis",
         rapl & kPowerLimitPl2Clamp ? "en" : "dis");
}

void clear_limit_reason_log() {
  // Limit reason MSR is supported on Intel Core generations 6 through 11, Intel Xeon generations
  // 1 through 3, Intel Core i3 8th generation, and Intel Xeon E processors. See Intel Volume 4
  // Table 2-39.
  auto x86_microarch = x86_get_microarch_config()->x86_microarch;
  if ((x86_microarch != X86_MICROARCH_INTEL_SKYLAKE) &&
      (x86_microarch != X86_MICROARCH_INTEL_CANNONLAKE) &&
      (x86_microarch != X86_MICROARCH_INTEL_TIGERLAKE)) {
    printf("Limit reasons msr not supported\n");
    return;
  }

  // The limit reason log is stored in bits 29:16 and can be cleared by writing zeros.
  MsrAccess msr;
  msr.write_msr(X86_MSR_PERF_LIMIT_REASONS, 0);
  msr.write_msr(X86_MSR_GFX_PERF_LIMIT_REASONS, 0);
}

void print_limit_reasons(bool use_log) {
  // Limit reason MSR is supported on Intel Core generations 6 through 11, Intel Xeon generations
  // 1 through 3, Intel Core i3 8th generation, and Intel Xeon E processors. See Intel Volume 4
  // Table 2-39.
  auto x86_microarch = x86_get_microarch_config()->x86_microarch;
  if ((x86_microarch != X86_MICROARCH_INTEL_SKYLAKE) &&
      (x86_microarch != X86_MICROARCH_INTEL_CANNONLAKE) &&
      (x86_microarch != X86_MICROARCH_INTEL_TIGERLAKE)) {
    printf("Limit reasons msr not supported\n");
    return;
  }

  MsrAccess msr;
  uint64_t limit_reasons = msr.read_msr(X86_MSR_PERF_LIMIT_REASONS);

  // The log bits (29:16) are latched versions of the status bits (13:0). If we're printing the log
  // shift the register value down.
  if (use_log) {
    limit_reasons = limit_reasons >> kLimitReasonsLogShift;
  }

  bool is_limited = false;
  printf("perf limit reasons:\n");
  for (auto reason : kLimitReasons) {
    if (!(limit_reasons & reason.bit)) {
      continue;
    }
    printf("\t%s\n", reason.str);
    is_limited = true;
  }
  if (!is_limited) {
    printf("\tnone\n");
  }

  limit_reasons = msr.read_msr(X86_MSR_GFX_PERF_LIMIT_REASONS);
  if (use_log) {
    limit_reasons = limit_reasons >> kLimitReasonsLogShift;
  }

  printf("gfx perf limit reasons:\n");
  is_limited = false;
  for (auto reason : kLimitReasonsGfx) {
    if (!(limit_reasons & reason.bit)) {
      continue;
    }
    printf("\t%s\n", reason.str);
    is_limited = true;
  }
  if (!is_limited) {
    printf("\tnone\n");
  }
}

RecurringCallback g_status_callback([]() {
  MsrAccess msr;
  rapl_units units = GetUnits(&msr);

  static uint64_t last_energy_status = 0;
  uint64_t energy_status = read_msr(X86_MSR_PKG_ENERGY_STATUS);
  printf("energy consumed: %luuJ (total: %luuJ)\n",
         (energy_status - last_energy_status) * units.energy_uj, energy_status * units.energy_uj);
  last_energy_status = energy_status;

  print_limit_reasons(/*use_log=*/false);
});

void print_command_usage() {
  static const struct {
    const char* cmd_str;
    const char* help_str;
  } subcommands[] = {
      {"status", "toggle status display"},
      {"limitreason clear", "clear the cpu limit reason log"},
      {"limitreason log", "print all cpu limit reasons since last clear"},
      {"limits", "print package power limits"},
  };
  printf("usage:\n");
  for (auto subcommand : subcommands) {
    printf("\tpower %-32s: %s\n", subcommand.cmd_str, subcommand.help_str);
  }
}

// This thread performs the work for suspend/resume.  We use a separate thread
// rather than the invoking thread to let us lean on the context switch code
// path to persist all of the usermode thread state that is not saved on a plain
// mode switch.
zx_status_t suspend_thread(void* raw_arg) {
  auto arg = reinterpret_cast<const zx_system_powerctl_arg_t*>(raw_arg);
  uint8_t target_s_state = arg->acpi_transition_s_state.target_s_state;
  uint8_t sleep_type_a = arg->acpi_transition_s_state.sleep_type_a;
  uint8_t sleep_type_b = arg->acpi_transition_s_state.sleep_type_b;

  return PlatformSuspend(target_s_state, sleep_type_a, sleep_type_b);
}

zx_status_t acpi_transition_s_state(const zx_system_powerctl_arg_t* arg) {
  uint8_t target_s_state = arg->acpi_transition_s_state.target_s_state;
  if (target_s_state == 0 || target_s_state > 5) {
    TRACEF("Bad S-state: S%u\n", target_s_state);
    return ZX_ERR_INVALID_ARGS;
  }

  // If not a shutdown, ensure CPU 0 is the only cpu left running.
  if (target_s_state != 5 && mp_get_online_mask() != cpu_num_to_mask(0)) {
    TRACEF("Too many CPUs running for state S%u\n", target_s_state);
    return ZX_ERR_BAD_STATE;
  }

  // Currently only transitioning to the S3 state is supported.
  if (target_s_state != 3) {
    return ZX_ERR_NOT_SUPPORTED;
  }

  // Prepare a resume path and execute the suspend on a separate thread (see comment on
  // |suspend_thread()| for explanation).
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

  return ZX_OK;
}

}  // namespace

zx_status_t arch_system_powerctl(uint32_t cmd, const zx_system_powerctl_arg_t* arg,
                                 MsrAccess* msr) {
  switch (cmd) {
    case ZX_SYSTEM_POWERCTL_ACPI_TRANSITION_S_STATE:
      if (gBootOptions->x86_enable_suspend) {
        return acpi_transition_s_state(arg);
      } else {
        return ZX_ERR_NOT_SUPPORTED;
      }
    case ZX_SYSTEM_POWERCTL_X86_SET_PKG_PL1:
      return SetPkgPl1(arg, msr);
    default:
      return ZX_ERR_NOT_SUPPORTED;
  }
}

static zx_status_t cmd_power(int argc, const cmd_args* argv, uint32_t flags) {
  if (argc < 2) {
    print_command_usage();
    return ZX_ERR_INVALID_ARGS;
  }

  if (!strcmp(argv[1].str, "status")) {
    g_status_callback.Toggle();
    return ZX_OK;
  } else if (!strcmp(argv[1].str, "limitreason")) {
    if (argc < 3) {
      print_command_usage();
      return ZX_ERR_INVALID_ARGS;
    }
    if (!strcmp(argv[2].str, "log")) {
      print_limit_reasons(/*use_log=*/true);
      return ZX_OK;
    } else if (!strcmp(argv[2].str, "clear")) {
      clear_limit_reason_log();
      return ZX_OK;
    }
  } else if (!strcmp(argv[1].str, "limits")) {
    print_limits();
    return ZX_OK;
  }

  print_command_usage();
  return ZX_ERR_INVALID_ARGS;
}

STATIC_COMMAND_START
STATIC_COMMAND("power", "power limiting debug commands (for x86 only)", &cmd_power)
STATIC_COMMAND_END(cpu)
