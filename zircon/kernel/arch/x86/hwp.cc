// Copyright 2017 The Fuchsia Authors
// Copyright (c) 2016 Travis Geiselbrecht
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <inttypes.h>
#include <lib/console.h>
#include <string.h>
#include <zircon/compiler.h>
#include <zircon/errors.h>
#include <zircon/types.h>

#include <arch/x86/cpuid.h>
#include <arch/x86/feature.h>
#include <arch/x86/hwp.h>
#include <arch/x86/platform_access.h>
#include <fbl/bits.h>
#include <fbl/hard_int.h>
#include <kernel/lockdep.h>
#include <kernel/mp.h>
#include <kernel/spinlock.h>
#include <ktl/optional.h>
#include <ktl/string_view.h>

DECLARE_SINGLETON_MUTEX(hwp_lock);

namespace x86 {
namespace {

// An "energy performance preference" is a 8-bit value specifying a desired
// tradeoff between running a CPU in a high performance mode (0) vs an
// energy-efficient mode (255).
DEFINE_HARD_INT(EnergyPerformancePref, uint8_t)

// Various EnergyPerformancePref values.
constexpr auto kMaxPerformanceEPP = EnergyPerformancePref(0x00);
constexpr auto kBalancedEPP = EnergyPerformancePref(0x80);
constexpr auto kPowerSaveEPP = EnergyPerformancePref(0xff);

// An 8-bit "performance level", as used by the IA32_HWP_CAPABILITIES
// MSR. Higher values indicate higher performance, at the cost of using
// more power.
DEFINE_HARD_INT(PerformanceLevel, uint8_t)

// Convert the 4-bit IA32_ENERGY_PERF_BIAS value into an 8-bit
// IA32_ENERGY_PERF_PREFERENCE value.
//
// IA32_ENERGY_PERF_BIAS is a 4-bit value that may be set by firmware to
// indicate a platform's desired tradeoff between performance and power
// efficiency. It is only used when HWP is not active, so we convert it to HWP's
// ENERGY_PERFORMANCE_PREFERENCE scale.
EnergyPerformancePref PerfBiasToPerfPref(uint8_t epb) {
  static constexpr uint8_t energy_perf_bias_to_energy_perf_preference[] = {
      /* 0x0 */ 0x20,  // 'PERFORMANCE'
      /* 0x1 */ 0x20,
      /* 0x2 */ 0x20,
      /* 0x3 */ 0x20,
      /* 0x4 */ 0x40,  // 'BALANCED PERFORMANCE'
      /* 0x5 */ 0x40,
      /* 0x6 */ 0x80,  // 'NORMAL'
      /* 0x7 */ 0x80,
      /* 0x8 */ 0x80,  // 'BALANCED POWERSAVE'
      /* 0x9 */ 0xFF,
      /* 0xA */ 0xFF,
      /* 0xB */ 0xFF,
      /* 0xC */ 0xFF,
      /* 0xD */ 0xFF,
      /* 0xE */ 0xFF,
      /* 0xF */ 0xFF,  // 'POWERSAVE'
  };
  static_assert(sizeof(energy_perf_bias_to_energy_perf_preference) == 16);

  epb &= 0xF;  // Sanitize ENERGY_PERF_BIAS just in case.
  return EnergyPerformancePref(energy_perf_bias_to_energy_perf_preference[epb]);
}

// Hardware-recommended EnergencyPerformancePref values.
struct HwpCapabilities {
  PerformanceLevel most_efficient_performance;
  PerformanceLevel guaranteed_performance;
  PerformanceLevel highest_performance;
  PerformanceLevel lowest_performance;
};

// Parse the HWP capabilties of the CPU.
HwpCapabilities ReadHwpCapabilities(MsrAccess* msr) {
  uint64_t hwp_caps = msr->read_msr(X86_MSR_IA32_HWP_CAPABILITIES);

  HwpCapabilities result;
  result.highest_performance = PerformanceLevel(fbl::ExtractBits<7, 0, uint8_t>(hwp_caps));
  result.guaranteed_performance = PerformanceLevel(fbl::ExtractBits<15, 8, uint8_t>(hwp_caps));
  result.most_efficient_performance = PerformanceLevel(fbl::ExtractBits<23, 16, uint8_t>(hwp_caps));
  result.lowest_performance = PerformanceLevel(fbl::ExtractBits<31, 24, uint8_t>(hwp_caps));

  return result;
}

// Return the EnergyPerformancePref recommended by the BIOS/firmware.
EnergyPerformancePref GetBiosEPP(const cpu_id::CpuId* cpuid, MsrAccess* msr) {
  if (cpuid->ReadFeatures().HasFeature(cpu_id::Features::EPB)) {
    return PerfBiasToPerfPref(msr->read_msr(X86_MSR_IA32_ENERGY_PERF_BIAS) & 0xF);
  }
  return kBalancedEPP;
}

// Construct a 64-bit MSR_REQUEST MSR value.
uint64_t MakeHwpRequest(PerformanceLevel min_perf, PerformanceLevel max_perf,
                        PerformanceLevel desired_perf, EnergyPerformancePref epp) {
  return static_cast<uint64_t>(min_perf.value()) |
         (static_cast<uint64_t>(max_perf.value()) << 8ull) |
         (static_cast<uint64_t>(desired_perf.value()) << 16ull) |
         (static_cast<uint64_t>(epp.value()) << 24ull);
}

}  // namespace

ktl::optional<IntelHwpPolicy> IntelHwpParsePolicy(const char* str) {
  if (str == nullptr) {
    return ktl::nullopt;
  }

  static const constexpr struct IntelHwpPolicyName {
    IntelHwpPolicy policy;
    ktl::string_view name;
  } kHwpPolicyNames[] = {
      {IntelHwpPolicy::kBiosSpecified, "bios-specified"sv},
      {IntelHwpPolicy::kPerformance, "performance"sv},
      {IntelHwpPolicy::kBalanced, "balanced"sv},
      {IntelHwpPolicy::kPowerSave, "power-save"sv},
      {IntelHwpPolicy::kStablePerformance, "stable-performance"sv},
  };
  ktl::string_view s = ktl::string_view(str);
  for (const IntelHwpPolicyName item : kHwpPolicyNames) {
    if (s == item.name) {
      return item.policy;
    }
  }
  return ktl::nullopt;
}

void IntelHwpInit(const cpu_id::CpuId* cpuid, MsrAccess* msr, IntelHwpPolicy policy) {
  // Ensure we have HWP on this CPU.
  if (!IntelHwpSupported(cpuid)) {
    return;
  }

  // Enable HWP.
  msr->write_msr(X86_MSR_IA32_PM_ENABLE, 1);

  // Get hardware capabilities.
  HwpCapabilities caps = ReadHwpCapabilities(msr);

  // Set up HWP preferences.
  //
  // In most cases, we set minimum/maximum to values from the corresponding
  // capabilities, set desired performance to 0 ("automatic"),  and set the
  // energy performance based on the policy.
  //
  // Reference: Intel SDM vol 3B section 14.4.7: Recommendations for OS use of
  // HWP controls
  PerformanceLevel desired = PerformanceLevel(0);  // auto
  PerformanceLevel min = caps.lowest_performance;
  PerformanceLevel max = caps.highest_performance;
  EnergyPerformancePref pref = kMaxPerformanceEPP;

  // Override defaults based on policy.
  switch (policy) {
    case IntelHwpPolicy::kBiosSpecified:
      pref = GetBiosEPP(cpuid, msr);
      break;
    case IntelHwpPolicy::kPerformance:
      pref = kMaxPerformanceEPP;
      break;
    case IntelHwpPolicy::kBalanced:
      pref = kBalancedEPP;
      break;
    case IntelHwpPolicy::kPowerSave:
      pref = kPowerSaveEPP;
      break;
    case IntelHwpPolicy::kStablePerformance:
      // Set min/max/desired to "guaranteed_performance" to try and keep CPU at
      // a stable performance level.
      min = caps.guaranteed_performance;
      max = caps.guaranteed_performance;
      desired = caps.guaranteed_performance;
      pref = kMaxPerformanceEPP;
      break;
  }

  // Program the HWP request register.
  msr->write_msr(X86_MSR_IA32_HWP_REQUEST, MakeHwpRequest(/*min_perf=*/min, /*max_perf=*/max,
                                                          /*desired_perf=*/desired, /*epp=*/pref));
}

bool IntelHwpSupported(const cpu_id::CpuId* cpuid) {
  return cpuid->ReadFeatures().HasFeature(cpu_id::Features::HWP) &&
         cpuid->ReadFeatures().HasFeature(cpu_id::Features::HWP_PREF);
}

namespace {

void CmdPrintUsage() {
  printf(
      "usage:\n"
      "  hwp set-policy <policy-name>  - set performance policy\n"
      "                                  valid policies include bios-specified, performance,\n"
      "                                  balanced, power-save, stable-performance.\n"
      "  hwp set-freq <int>            - set processor frequency to given value.\n"
      "                                  values map directly onto frequency targets, but the \n"
      "                                  exact meaning is processor-dependant.\n");
}

zx_status_t CmdSetPolicy(int argc, const cmd_args* argv, uint32_t flags) {
  if (argc != 1) {
    CmdPrintUsage();
    return ZX_ERR_INVALID_ARGS;
  }

  std::optional<IntelHwpPolicy> policy = IntelHwpParsePolicy(argv[0].str);
  if (!policy.has_value()) {
    printf("Unknown policy '%s'.\n", argv[0].str);
    return ZX_ERR_INVALID_ARGS;
  }

  mp_sync_exec(
      MP_IPI_TARGET_ALL, 0,
      [](void* policy_ptr) {
        auto policy = reinterpret_cast<std::optional<IntelHwpPolicy>*>(policy_ptr)->value();
        cpu_id::CpuId cpuid;
        MsrAccess msr;
        IntelHwpInit(&cpuid, &msr, policy);
      },
      &policy);

  printf("Policy updated to '%s'.\n", argv[0].str);
  return ZX_OK;
}

zx_status_t CmdSetFreq(int argc, const cmd_args* argv, uint32_t flags) {
  if (argc != 1) {
    CmdPrintUsage();
    return ZX_ERR_INVALID_ARGS;
  }

  unsigned long desired_freq = argv[0].u;
  if (desired_freq == 0 || desired_freq >= 256) {
    printf("Invalid frequency target.\n");
    return ZX_ERR_INVALID_ARGS;
  }

  mp_sync_exec(
      MP_IPI_TARGET_ALL, 0,
      [](void* desired_freq_ptr) {
        auto desired_freq = PerformanceLevel(
            static_cast<uint8_t>(*reinterpret_cast<unsigned long*>(desired_freq_ptr)));
        MsrAccess msr;
        msr.write_msr(X86_MSR_IA32_HWP_REQUEST,
                      MakeHwpRequest(/*min_perf=*/desired_freq, /*max_perf=*/desired_freq,
                                     /*desired_perf=*/desired_freq, /*epp=*/kMaxPerformanceEPP));
      },
      &desired_freq);

  printf("Frequency set to target %lu.\n", desired_freq);
  return ZX_OK;
}

zx_status_t CmdHwp(int argc, const cmd_args* argv, uint32_t flags) {
  // Ensure we have the hardware.
  cpu_id::CpuId cpuid;
  if (!IntelHwpSupported(&cpuid)) {
    printf("HWP not supported on system.\n");
    return ZX_ERR_NOT_SUPPORTED;
  }

  // Each command needs at least two tokens: "hwp <subcommand>".
  if (argc < 2) {
    CmdPrintUsage();
    return ZX_ERR_INVALID_ARGS;
  }

  std::string_view subcommand(argv[1].str);
  if (subcommand == "set-policy"sv) {
    return CmdSetPolicy(argc - 2, argv + 2, flags);
  }

  if (subcommand == "set-freq"sv) {
    return CmdSetFreq(argc - 2, argv + 2, flags);
  }

  CmdPrintUsage();
  return ZX_ERR_INVALID_ARGS;
}

}  // namespace
}  // namespace x86

STATIC_COMMAND_START
STATIC_COMMAND("hwp", "hardware controlled performance states\n", &x86::CmdHwp)
STATIC_COMMAND_END(hwp)
