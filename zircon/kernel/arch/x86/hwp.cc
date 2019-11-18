// Copyright 2017 The Fuchsia Authors
// Copyright (c) 2016 Travis Geiselbrecht
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <err.h>
#include <inttypes.h>
#include <lib/console.h>
#include <string.h>
#include <zircon/compiler.h>

#include <arch/x86/cpuid.h>
#include <arch/x86/feature.h>
#include <arch/x86/hwp.h>
#include <arch/x86/platform_access.h>
#include <kernel/lockdep.h>
#include <kernel/mp.h>
#include <kernel/spinlock.h>

DECLARE_SINGLETON_MUTEX(hwp_lock);

static uint8_t x86_intel_epb_to_epp(uint8_t epb) {
  // IA32_ENERGY_PERF_BIAS is a 4-bit value that may be set by firmware to indicate a platform's
  // desired tradeoff between performance and power efficiency. It is only used when HWP is not
  // active, so we convert it to HWP's ENERGY_PERFORMANCE_PREFERENCE scale.
  static constexpr uint8_t energy_perf_bias_to_energy_perf_preference[] = {
    0x20,  // ENERGY_PERF_BIAS 0x0 is 'PERFORMANCE'
    0x20,
    0x20,
    0x20,
    0x40,  // ENERGY_PERF_BIAS 0x4 is 'BALANCED PERFORMANCE'
    0x40,
    0x80,  // ENERGY_PERF_BIAS 0x6 is 'NORMAL'
    0x80,
    0x80,  // ENERGY_PERF_BIAS 0x8 is 'BALANCED POWERSAVE'

    0xFF,
    0xFF,
    0xFF,
    0xFF,
    0xFF,
    0xFF,
    0xFF   // ENERGY_PERF_BIAS 0xF is 'POWERSAVE'
  };
  static_assert(sizeof(energy_perf_bias_to_energy_perf_preference) == 16, "");

  epb &= 0xF;  // Sanitize ENERGY_PERF_BIAS just in case.
  return energy_perf_bias_to_energy_perf_preference[epb];
}

void x86_intel_hwp_init(const cpu_id::CpuId* cpuid, MsrAccess* msr) {
  static constexpr uint8_t kBalancedEPP = 0x80;  // default

  if (!cpuid->ReadFeatures().HasFeature(cpu_id::Features::HWP_PREF)) {
    return;
  }

  // If available, use firmware-set IA32_ENERGY_PERF_BIAS to select Energy/Performance Preference
  bool has_epb = cpuid->ReadFeatures().HasFeature(cpu_id::Features::EPB);
  uint8_t epb;
  if (has_epb) {
    epb = msr->read_msr(X86_MSR_IA32_ENERGY_PERF_BIAS) & 0xF;
  }

  // Enable HWP
  msr->write_msr(X86_MSR_IA32_PM_ENABLE, 1);

  // Set minimum/maximum to values from capabilities for common case, set Desired_Performance to 0,
  // and set energy performance preference from IA32_ENERGY_PERF_BIAS (if set) or to the default
  // value if not.
  // Reference: Intel SDM vol 3B section 14.4.10: Recommendations for OS use of HWP controls
  uint64_t hwp_caps = msr->read_msr(X86_MSR_IA32_HWP_CAPABILITIES);
  uint8_t max_performance = hwp_caps & 0xff;
  uint8_t min_performance = (hwp_caps >> 24) & 0xff;
  uint8_t energy_perf_preference = has_epb ? x86_intel_epb_to_epp(epb) : kBalancedEPP;
  uint64_t hwp_req = static_cast<uint32_t>(energy_perf_preference << 24u) |
                     (max_performance << 8) | (min_performance);
  msr->write_msr(X86_MSR_IA32_HWP_REQUEST, hwp_req);
}

static void hwp_set_hint_sync_task(void* ctx) {
  uint8_t hint = (unsigned long)ctx & 0xff;
  uint64_t hwp_req = read_msr(X86_MSR_IA32_HWP_REQUEST) & ~(0xff << 16);
  hwp_req |= (hint << 16);
  hwp_req &= ~(0xffffffffull << 32);
  write_msr(X86_MSR_IA32_HWP_REQUEST, hwp_req);
}

static void hwp_set_desired_performance(unsigned long hint) {
  Guard<Mutex> guard{hwp_lock::Get()};

  if (!x86_feature_test(X86_FEATURE_HWP_PREF)) {
    printf("HWP hint not supported\n");
    return;
  }
  mp_sync_exec(MP_IPI_TARGET_ALL, 0, hwp_set_hint_sync_task, (void*)hint);
}

static int cmd_hwp(int argc, const cmd_args* argv, uint32_t flags) {
  if (argc < 2) {
  notenoughargs:
    printf("not enough arguments\n");
  usage:
    printf("usage:\n");
    printf("%s hint <1-255>: set clock speed hint (as a multiple of 100MHz)\n", argv[0].str);
    printf("%s hint 0: enable autoscaling\n", argv[0].str);
    return ZX_ERR_INTERNAL;
  }

  if (!strcmp(argv[1].str, "hint")) {
    if (argc < 3) {
      goto notenoughargs;
    }
    if (argv[2].u > 0xff) {
      printf("hint must be between 0 and 255\n");
      goto usage;
    }
    hwp_set_desired_performance(argv[2].u);
  } else {
    printf("unknown command\n");
    goto usage;
  }

  return ZX_OK;
}

STATIC_COMMAND_START
STATIC_COMMAND("hwp", "hardware controlled performance states\n", &cmd_hwp)
STATIC_COMMAND_END(hwp)
