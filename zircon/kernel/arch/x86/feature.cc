// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT
#include "arch/x86/feature.h"

#include <assert.h>
#include <bits.h>
#include <lib/arch/x86/boot-cpuid.h>
#include <lib/arch/x86/bug.h>
#include <lib/arch/x86/cache.h>
#include <lib/arch/x86/extension.h>
#include <lib/arch/x86/feature.h>
#include <lib/arch/x86/power.h>
#include <lib/arch/x86/speculation.h>
#include <lib/boot-options/boot-options.h>
#include <stdint.h>
#include <string.h>
#include <trace.h>

#include <arch/ops.h>
#include <arch/x86/cpuid.h>
#include <arch/x86/hwp.h>
#include <arch/x86/mmu.h>
#include <arch/x86/platform_access.h>
#include <arch/x86/pv.h>
#include <fbl/algorithm.h>
#include <hwreg/x86msr.h>
#include <ktl/atomic.h>
#include <ktl/iterator.h>
#include <platform/pc/bootbyte.h>

#include <ktl/enforce.h>

#define LOCAL_TRACE 0

struct cpuid_leaf _cpuid[MAX_SUPPORTED_CPUID + 1];
struct cpuid_leaf _cpuid_hyp[MAX_SUPPORTED_CPUID_HYP - X86_CPUID_HYP_BASE + 1];
struct cpuid_leaf _cpuid_ext[MAX_SUPPORTED_CPUID_EXT - X86_CPUID_EXT_BASE + 1];
uint32_t max_cpuid = 0;
uint32_t max_hyp_cpuid = 0;
uint32_t max_ext_cpuid = 0;

enum x86_vendor_list x86_vendor;
const x86_microarch_config_t* x86_microarch_config;

static struct x86_model_info model_info;

bool g_x86_feature_fsgsbase;
bool g_x86_feature_pcid_good;
bool g_x86_feature_has_smap;
bool g_has_meltdown;
bool g_has_l1tf;
bool g_l1d_flush_on_vmentry;
bool g_has_mds_taa;
bool g_has_swapgs_bug;
bool g_has_ssb;
bool g_has_ssbd;
bool g_ssb_mitigated;
bool g_has_md_clear;
bool g_md_clear_on_user_return;
bool g_has_spec_ctrl;
bool g_has_ibpb;
bool g_should_ibpb_on_ctxt_switch;
bool g_ras_fill_on_ctxt_switch;
bool g_cpu_vulnerable_to_rsb_underflow;
bool g_has_enhanced_ibrs;
bool g_has_retbleed;
bool g_stibp_enabled;

enum x86_hypervisor_list x86_hypervisor;
bool g_hypervisor_has_pv_clock;
bool g_hypervisor_has_pv_eoi;
bool g_hypervisor_has_pv_ipi;

static ktl::atomic<bool> g_cpuid_initialized;

static enum x86_hypervisor_list get_hypervisor();

void x86_feature_early_init_percpu(void) {
  if (g_cpuid_initialized.exchange(true)) {
    return;
  }
  /* test for cpuid count */
  cpuid(0, &_cpuid[0].a, &_cpuid[0].b, &_cpuid[0].c, &_cpuid[0].d);

  max_cpuid = _cpuid[0].a;
  if (max_cpuid > MAX_SUPPORTED_CPUID)
    max_cpuid = MAX_SUPPORTED_CPUID;

  LTRACEF("max cpuid 0x%x\n", max_cpuid);

  /* figure out the vendor */
  union {
    uint32_t vendor_id[3];
    char vendor_string[12];
  } vu;
  vu.vendor_id[0] = _cpuid[0].b;
  vu.vendor_id[1] = _cpuid[0].d;
  vu.vendor_id[2] = _cpuid[0].c;
  if (!memcmp(vu.vendor_string, "GenuineIntel", sizeof(vu.vendor_string))) {
    x86_vendor = X86_VENDOR_INTEL;
  } else if (!memcmp(vu.vendor_string, "AuthenticAMD", sizeof(vu.vendor_string))) {
    x86_vendor = X86_VENDOR_AMD;
  } else {
    x86_vendor = X86_VENDOR_UNKNOWN;
  }

  /* read in the base cpuids */
  for (uint32_t i = 1; i <= max_cpuid; i++) {
    cpuid_c(i, 0, &_cpuid[i].a, &_cpuid[i].b, &_cpuid[i].c, &_cpuid[i].d);
  }

  /* test for extended cpuid count */
  cpuid(X86_CPUID_EXT_BASE, &_cpuid_ext[0].a, &_cpuid_ext[0].b, &_cpuid_ext[0].c, &_cpuid_ext[0].d);

  max_ext_cpuid = _cpuid_ext[0].a;
  LTRACEF("max extended cpuid 0x%x\n", max_ext_cpuid);
  if (max_ext_cpuid > MAX_SUPPORTED_CPUID_EXT)
    max_ext_cpuid = MAX_SUPPORTED_CPUID_EXT;

  /* read in the extended cpuids */
  for (uint32_t i = X86_CPUID_EXT_BASE + 1; i - 1 < max_ext_cpuid; i++) {
    uint32_t index = i - X86_CPUID_EXT_BASE;
    cpuid_c(i, 0, &_cpuid_ext[index].a, &_cpuid_ext[index].b, &_cpuid_ext[index].c,
            &_cpuid_ext[index].d);
  }

  /* read in the hypervisor cpuids. the maximum leaf is reported at X86_CPUID_HYP_BASE. */
  cpuid(X86_CPUID_HYP_VENDOR, &_cpuid_ext[0].a, &_cpuid_ext[0].b, &_cpuid_ext[0].c,
        &_cpuid_ext[0].d);
  max_hyp_cpuid = _cpuid_ext[0].a;
  if (max_hyp_cpuid > MAX_SUPPORTED_CPUID_HYP)
    max_hyp_cpuid = MAX_SUPPORTED_CPUID_HYP;
  for (uint32_t i = X86_CPUID_HYP_BASE; i <= max_hyp_cpuid; i++) {
    uint32_t index = i - X86_CPUID_HYP_BASE;
    cpuid(i, &_cpuid_hyp[index].a, &_cpuid_hyp[index].b, &_cpuid_hyp[index].c,
          &_cpuid_hyp[index].d);
  }

  /* populate the model info */
  const struct cpuid_leaf* leaf = x86_get_cpuid_leaf(X86_CPUID_MODEL_FEATURES);
  if (leaf) {
    model_info.processor_type = (uint8_t)BITS_SHIFT(leaf->a, 13, 12);
    model_info.family = (uint8_t)BITS_SHIFT(leaf->a, 11, 8);
    model_info.model = (uint8_t)BITS_SHIFT(leaf->a, 7, 4);
    model_info.stepping = (uint8_t)BITS_SHIFT(leaf->a, 3, 0);
    model_info.display_family = model_info.family;
    model_info.display_model = model_info.model;

    if (model_info.family == 0xf) {
      model_info.display_family += BITS_SHIFT(leaf->a, 27, 20);
    }

    if (model_info.family == 0xf || model_info.family == 0x6) {
      model_info.display_model += BITS_SHIFT(leaf->a, 19, 16) << 4;
    }
  }

  cpu_id::CpuId cpuid;
  x86_microarch_config = get_microarch_config(&cpuid);
  x86_hypervisor = get_hypervisor();
  g_hypervisor_has_pv_clock =
      x86_hypervisor == X86_HYPERVISOR_KVM && x86_feature_test(X86_FEATURE_KVM_PV_CLOCK);
  g_hypervisor_has_pv_eoi =
      x86_hypervisor == X86_HYPERVISOR_KVM && x86_feature_test(X86_FEATURE_KVM_PV_EOI);
  g_hypervisor_has_pv_ipi =
      x86_hypervisor == X86_HYPERVISOR_KVM && x86_feature_test(X86_FEATURE_KVM_PV_IPI);
  g_x86_feature_has_smap = x86_feature_test(X86_FEATURE_SMAP);
  g_x86_feature_fsgsbase = x86_feature_test(X86_FEATURE_FSGSBASE);
  g_x86_feature_pcid_good =
      x86_feature_test(X86_FEATURE_PCID) && x86_feature_test(X86_FEATURE_INVPCID);
}

// Invoked on the boot CPU during boot, after platform is available.
void x86_cpu_feature_init() {
  DEBUG_ASSERT(arch_curr_cpu_num() == 0);

  arch::BootCpuidIo cpuid;
  hwreg::X86MsrIo msr;

  // TODO(61093): Replace with newer lib/arch and hwreg counterparts.
  cpu_id::CpuId cpuid_old;
  MsrAccess msr_old;

  // Get microcode patch level
  switch (x86_vendor) {
    case X86_VENDOR_INTEL:
      model_info.patch_level = x86_intel_get_patch_level();
      break;
    case X86_VENDOR_AMD:
      model_info.patch_level = x86_amd_get_patch_level();
      break;
    default:
      break;
  }

  g_has_swapgs_bug = arch::HasX86SwapgsBug(cpuid);

  // If mitigations are enabled, try to disable TSX. Disabling TSX prevents exploiting
  // TAA/CacheOut attacks and potential future exploits. It also avoids MD_CLEAR on CPUs
  // without MDS.
  //
  // WARNING: If we disable TSX, we must do so before we determine whether we are affected by
  // TAA/Cacheout; otherwise the TAA/Cacheout determination code will run before the TSX
  // CPUID bit is masked.
  if (!gBootOptions->x86_disable_spec_mitigations && arch::DisableTsx(cpuid, msr)) {
    // If successful, repopulate the boot CPU's CPUID cache in order to reflect
    // the disabling.
    arch::InitializeBootCpuid();
  }

  g_has_md_clear = cpuid.Read<arch::CpuidExtendedFeatureFlagsD>().md_clear();
  g_has_mds_taa = arch::HasX86MdsTaaBugs(cpuid, msr);
  g_md_clear_on_user_return = !gBootOptions->x86_disable_spec_mitigations && g_has_mds_taa &&
                              g_has_md_clear && gBootOptions->x86_md_clear_on_user_return;
  g_has_spec_ctrl = arch::SpeculationControlMsr::IsSupported(cpuid);
  g_has_ssb = arch::HasX86SsbBug(cpuid, msr);
  g_has_ssbd = arch::CanMitigateX86SsbBug(cpuid);
  g_ssb_mitigated = !gBootOptions->x86_disable_spec_mitigations && g_has_ssb && g_has_ssbd &&
                    gBootOptions->x86_spec_store_bypass_disable;
  g_has_ibpb = arch::HasIbpb(cpuid);
  g_has_enhanced_ibrs = arch::HasIbrs(cpuid, msr, /*always_on_mode=*/true);
  g_has_meltdown = arch::HasX86MeltdownBug(cpuid, msr);
  g_has_l1tf = arch::HasX86L1tfBug(cpuid, msr);
  g_l1d_flush_on_vmentry = !gBootOptions->x86_disable_spec_mitigations && g_has_l1tf &&
                           arch::BootCpuid<arch::CpuidExtendedFeatureFlagsD>().l1d_flush();
  g_ras_fill_on_ctxt_switch = !gBootOptions->x86_disable_spec_mitigations;
  g_cpu_vulnerable_to_rsb_underflow = !gBootOptions->x86_disable_spec_mitigations &&
                                      (x86_vendor == X86_VENDOR_INTEL) &&
                                      x86_intel_cpu_has_rsb_fallback(&cpuid_old, &msr_old);
  // TODO(fxbug.dev/33667, fxbug.dev/12150): Consider whether a process can opt-out of an IBPB on
  // switch, either on switch-in (ex: its compiled with a retpoline) or switch-out (ex: it promises
  // not to attack the next process).
  // TODO(fxbug.dev/33667, fxbug.dev/12150): Should we have an individual knob for IBPB?
  g_should_ibpb_on_ctxt_switch = !gBootOptions->x86_disable_spec_mitigations && g_has_ibpb;

  switch (x86_vendor) {
    case X86_VENDOR_INTEL:
      g_has_retbleed = false;  // TODO: Enumerate Intel CPUs affected by RETBLEED.
      break;
    case X86_VENDOR_AMD:
      g_has_retbleed = x86_amd_has_retbleed();
      break;
    case X86_VENDOR_UNKNOWN:
      break;
  }
}

// Invoked on each CPU during boot, after platform init has taken place.
void x86_cpu_feature_late_init_percpu(void) {
  const bool on_boot_cpu = arch_curr_cpu_num() == 0;

  arch::BootCpuidIo cpuid;
  hwreg::X86MsrIo msr;

  // Same reasoning as was done in x86_cpu_feature_init() for the boot CPU.
  if (!gBootOptions->x86_disable_spec_mitigations && !on_boot_cpu) {
    arch::DisableTsx(cpuid, msr);
  }

  // Spectre v2 hardware-related mitigations; retpolines may further be used,
  // which is taken care of by the code-patching engine.
  bool stibp_enabled = false;
  if (!gBootOptions->x86_disable_spec_mitigations) {
    switch (arch::GetPreferredSpectreV2Mitigation(cpuid, msr)) {
      case arch::SpectreV2Mitigation::kIbrs:  // Enhanced IBRS
        arch::EnableIbrs(cpuid, msr);
        break;
      case arch::SpectreV2Mitigation::kIbpbRetpoline:
        break;
      case arch::SpectreV2Mitigation::kIbpbRetpolineStibp:
        // Enable STIPB for added cross-hyperthread security.
        stibp_enabled = true;
        arch::EnableStibp(cpuid, msr);
        break;
    }
  }
  // RETbleed mitigations
  // Some RETbleed mitigations may overlap with Spectre V2 mitigations.
  if (!gBootOptions->x86_disable_spec_mitigations && g_has_retbleed) {
    if (x86_vendor == X86_VENDOR_AMD) {
      if (arch::HasStibp(cpuid, false) && !stibp_enabled) {
        stibp_enabled = true;
        arch::EnableStibp(cpuid, msr);
      }
      x86_amd_zen2_retbleed_mitigation(model_info);
    }
    if (x86_vendor == X86_VENDOR_INTEL) {
      // TODO: Mitigate RETbleed on Intel processors.
    }
  }

  g_stibp_enabled |= stibp_enabled;

  // Mitigate Spectre v4 (Speculative Store Bypass) if requested.
  if (x86_cpu_should_mitigate_ssb()) {
    if (!arch::MitigateX86SsbBug(cpuid, msr)) {
      printf("failed to mitigate SSB (Speculative Store Bypass) vulnerability\n");
    }
  }

  // Enable/disable Turbo on the processor.
  if (arch::SetX86CpuTurboState(cpuid, msr, gBootOptions->x86_turbo)) {
    // Since IA32_MISC_ENABLE may be updated and leaf 0x6 references the
    // former's state, repopulate the boot CPUID cache.
    if (on_boot_cpu) {
      arch::InitializeBootCpuid();
      printf("Turbo performance boost: %s\n", gBootOptions->x86_turbo ? "enabled" : "disabled");
    }
  } else if (on_boot_cpu) {
    printf("Turbo performance boost: unsupported\n");
  }

  // TODO(fxbug.dev/61093): Replace with newer lib/arch and hwreg counterparts.
  cpu_id::CpuId cpuid_old;
  MsrAccess msr_old;

  // Set up hardware-controlled performance states.
  if (gBootOptions->x86_hwp) {
    x86::IntelHwpInit(&cpuid_old, &msr_old, gBootOptions->x86_hwp_policy);
  }

  // If we are running under a hypervisor and paravirtual EOI (PV_EOI) is available, enable it.
  if (x86_hypervisor_has_pv_eoi()) {
    PvEoi::get()->Enable(&msr_old);
  }
}

static enum x86_hypervisor_list get_hypervisor() {
  if (!x86_feature_test(X86_FEATURE_HYPERVISOR)) {
    return X86_HYPERVISOR_NONE;
  }
  uint32_t a, b, c, d;
  cpuid(X86_CPUID_HYP_VENDOR, &a, &b, &c, &d);
  union {
    uint32_t vendor_id[3];
    char vendor_string[12];
  } vu;
  vu.vendor_id[0] = b;
  vu.vendor_id[1] = c;
  vu.vendor_id[2] = d;
  if (a >= X86_CPUID_KVM_FEATURES &&
      !memcmp(vu.vendor_string, "KVMKVMKVM\0\0\0", sizeof(vu.vendor_string))) {
    return X86_HYPERVISOR_KVM;
  } else {
    return X86_HYPERVISOR_UNKNOWN;
  }
}

bool x86_get_cpuid_subleaf(enum x86_cpuid_leaf_num num, uint32_t subleaf, struct cpuid_leaf* leaf) {
  if (num < X86_CPUID_EXT_BASE) {
    if (num > max_cpuid)
      return false;
  } else if (num > max_ext_cpuid) {
    return false;
  }

  cpuid_c((uint32_t)num, subleaf, &leaf->a, &leaf->b, &leaf->c, &leaf->d);
  return true;
}

const struct x86_model_info* x86_get_model(void) { return &model_info; }

// Printable registers can take up quite a bit of unsafe stack space. By
// constructing them as temporary variables within in a separate,
// non-inline-able function, we ensure that only one such register lives on the
// stack at a given time across consecutive calls to print their fields.
template <typename RegisterType, typename PrintCallback>
[[gnu::noinline]] void PrintFields(PrintCallback& print_cb) {
  arch::BootCpuid<RegisterType>().ForEachField(print_cb);
}

void x86_feature_debug(void) {
  // Allows us to take advantage of custom print format specifiers, which the
  // compiler would otherwise complain about.
  auto Printf = [](const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    vprintf(fmt, args);
    va_end(args);
  };

  Printf("\n");

  arch::BootCpuidIo io;

  {
    arch::CpuCacheInfo caches(io);
    Printf("==== X86 CACHE INFO ====\n");
    Printf("%-5s | %-11s | %-10s | %-5s | %-6s |\n", "Level", "Type", "Size (KiB)", "Sets",
           "Assoc.");
    for (const auto& cache : caches) {
      Printf("L%-4zu | %-11V | %-10zu | %-5zu | %-6zu |\n", cache.level, arch::ToString(cache.type),
             cache.size_kb, cache.number_of_sets, cache.ways_of_associativity);
    }
    Printf("\n");
  }

  Printf("Vendor: %V\n", arch::ToString(arch::GetVendor(io)));
  Printf("Microarchitecture: %V\n", arch::ToString(arch::GetMicroarchitecture(io)));
  Printf("Processor: %V\n", arch::ProcessorName(io).name());
  {
    ktl::string_view hypervisor = arch::HypervisorName(io).name();
    Printf("Hypervisor: %V\n", hypervisor.empty() ? "None" : hypervisor);
  }

  const auto version = io.Read<arch::CpuidVersionInfo>();
  Printf("Family/Model/Stepping: %#x/%#x/%#x\n", version.family(), version.model(),
         version.stepping());
  Printf("Patch level: %x\n", model_info.patch_level);

  auto print_feature = [col = size_t{0}](const char* name, auto value, auto, auto) mutable {
    if (name && value) {
      col += printf("%s%s", col ? ", " : "", name);
      if (col >= 80) {
        printf("\n");
        col = 0;
      }
    }
  };

  Printf("\nFeatures:\n");
  PrintFields<arch::CpuidFeatureFlagsC>(print_feature);
  PrintFields<arch::CpuidFeatureFlagsD>(print_feature);
  PrintFields<arch::CpuidExtendedFeatureFlagsB>(print_feature);
  // TODO(fxbug.dev/68404): Print when we can afford to.
  // io.Read<arch::CpuidAmdFeatureFlagsC>().ForEachField(print_feature);
  Printf("\n");

  // Print synthetic 'features'/properties.
  auto print_property = [col = size_t{0}](const char* property, bool print = true) mutable {
    if (print) {
      col += printf("%s%s", col ? ", " : "", property);
      if (col >= 80) {
        printf("\n");
        col = 0;
      }
    }
  };
  Printf("\nProperties:\n");
  print_property("meltdown", g_has_meltdown);
  print_property("l1tf", g_has_l1tf);
  print_property("mds/taa", g_has_mds_taa);
  print_property("md_clear", g_has_md_clear);
  print_property("md_clear_user_return", g_md_clear_on_user_return);
  print_property("swapgs_bug", g_has_swapgs_bug);
  print_property("pcid_good", g_x86_feature_pcid_good);
  print_property("pti_enabled", x86_kpti_is_enabled());
  print_property("spec_ctrl", g_has_spec_ctrl);
  print_property("ssb", g_has_ssb);
  print_property("ssbd", g_has_ssbd);
  print_property("ssb_mitigated", g_ssb_mitigated);
  print_property("ibpb", g_has_ibpb);
  print_property("l1d_flush_on_vmentry", g_l1d_flush_on_vmentry);
  print_property("ibpb_ctxt_switch", g_should_ibpb_on_ctxt_switch);
  print_property("ras_fill", g_ras_fill_on_ctxt_switch);
  print_property("enhanced_ibrs", g_has_enhanced_ibrs);
#ifdef KERNEL_RETPOLINE
  print_property("retpoline");
#endif
#ifdef X64_KERNEL_JCC_WORKAROUND
  print_property("jcc_fix");
#endif
#ifdef HARDEN_SLS
  print_property("harden_sls");
#endif
  print_property("retbleed", g_has_retbleed);
  print_property("stibp_enabled", g_stibp_enabled);
  if (arch::BootCpuidSupports<arch::CpuidPerformanceMonitoringA>()) {
    const arch::CpuidPerformanceMonitoringA eax = io.Read<arch::CpuidPerformanceMonitoringA>();
    const arch::CpuidPerformanceMonitoringD edx = io.Read<arch::CpuidPerformanceMonitoringD>();
    if (eax.version() > 0) {
      printf("\narch_pmu version %u general purpose counters %u fixed counters %u\n", eax.version(),
             eax.num_general_counters(), edx.num_fixed_counters());
    }
  }
  Printf("\n\n");
}

// The highest priority mechanism to determine the apic frequency.
static uint64_t default_apic_freq() {
  // The APIC frequency is the core crystal clock frequency if it is
  // enumerated in the CPUID leaf 0x15, or the processor's bus clock
  // frequency.

  const struct cpuid_leaf* tsc_leaf = x86_get_cpuid_leaf(X86_CPUID_TSC);
  if (tsc_leaf && tsc_leaf->c != 0) {
    return tsc_leaf->c;
  }
  return 0;
}

// From Intel SDMv3 section 19.7.3 (Determining the Processor Base Frequency).
// For cores that have a hard coded bus frequency or crystal clock,
// fall back to this value if cpuid 15h doesn't fully specify it and we're not
// running in a hypervisor.
static uint64_t apic_freq_constant_fallback(const uint64_t hardcoded_apic_freq) {
  uint64_t v = default_apic_freq();
  if (v != 0) {
    return v;
  }
  if (x86_feature_test(X86_FEATURE_HYPERVISOR)) {
    return 0;
  }
  return hardcoded_apic_freq;
}

// From Intel SDMv3 section 19.7.3 (Determining the Processor Base Frequency).
static uint64_t skl_apic_freq() { return apic_freq_constant_fallback(24ul * 1000 * 1000); }

// From Intel SDMv3 section 19.7.3 (Determining the Processor Base Frequency).
static uint64_t skl_x_apic_freq() { return apic_freq_constant_fallback(25ul * 1000 * 1000); }

// From Intel SDMv3 section 19.7.3 (Determining the Processor Base Frequency).
static uint64_t bdw_apic_freq() { return apic_freq_constant_fallback(100ul * 1000 * 1000); }

static uint64_t bulldozer_apic_freq() {
  // 15h BKDG documents that is is 100Mhz.
  return apic_freq_constant_fallback(100ul * 1000 * 1000);
}

static uint64_t unknown_freq() { return 0; }

static uint64_t intel_tsc_freq() {
  const uint64_t core_crystal_clock_freq = x86_get_microarch_config()->get_apic_freq();

  // If this leaf is present, then 19.7.3 (Determining the Processor Base
  // Frequency) documents this as the nominal TSC frequency.
  const struct cpuid_leaf* tsc_leaf = x86_get_cpuid_leaf(X86_CPUID_TSC);
  if (tsc_leaf && tsc_leaf->a) {
    return (core_crystal_clock_freq * tsc_leaf->b) / tsc_leaf->a;
  }
  return 0;
}

static uint64_t amd_compute_p_state_clock(uint64_t p_state_msr) {
  // is it valid?
  if (!BIT(p_state_msr, 63))
    return 0;

  // different AMD microarchitectures use slightly different formulas to compute
  // the effective clock rate of a P state
  uint64_t clock = 0;
  switch (x86_microarch_config->x86_microarch) {
    case X86_MICROARCH_AMD_BULLDOZER:
    case X86_MICROARCH_AMD_JAGUAR: {
      uint64_t did = BITS_SHIFT(p_state_msr, 8, 6);
      uint64_t fid = BITS(p_state_msr, 5, 0);

      clock = (100 * (fid + 0x10) / (1 << did)) * 1000 * 1000;
      break;
    }
    case X86_MICROARCH_AMD_ZEN: {
      uint64_t fid = BITS(p_state_msr, 7, 0);

      clock = (fid * 25) * 1000 * 1000;
      break;
    }
    default:
      break;
  }

  return clock;
}

static uint64_t zen_tsc_freq() {
  const uint32_t p0_state_msr = 0xc0010064;  // base P-state MSR
  // According to the Family 17h PPR, the first P-state MSR is indeed
  // P0 state and appears to be experimentally so
  uint64_t p0_state;
  if (read_msr_safe(p0_state_msr, &p0_state) != ZX_OK)
    return 0;

  return amd_compute_p_state_clock(p0_state);
}

static void unknown_reboot_system(void) { return; }

static void unknown_reboot_reason(uint64_t) { return; }

static void hsw_reboot_system(void) {
  // 100-Series Chipset Reset Control Register: CPU + SYS Reset
  outp(0xcf9, 0x06);
}

static void hsw_reboot_reason(uint64_t reason) {
  bootbyte_set_reason(reason);

  // 100-Series Chipset Reset Control Register: CPU + SYS Reset
  // clear PCI reset sequence
  outp(0xcf9, 0x02);
  // discarded reads acting as a small delay on the bus
  (void)inp(0xcf9);
  (void)inp(0xcf9);
  outp(0xcf9, 0x04);
}

// Intel microarches
static const x86_microarch_config_t icelake_config{
    .x86_microarch = X86_MICROARCH_INTEL_ICELAKE,

    .get_apic_freq = default_apic_freq,
    .get_tsc_freq = intel_tsc_freq,
    .reboot_system = hsw_reboot_system,
    .reboot_reason = hsw_reboot_reason,
    .disable_c1e = true,
    .idle_prefer_hlt = false,
    .idle_states =
        {
            .states = {X86_CSTATE_C1(0)},
            .default_state_mask = kX86IdleStateMaskC1Only,
        },
};

static const x86_microarch_config_t tiger_lake_config{
    .x86_microarch = X86_MICROARCH_INTEL_TIGERLAKE,
    .get_apic_freq = default_apic_freq,
    .get_tsc_freq = intel_tsc_freq,
    .reboot_system = hsw_reboot_system,
    .reboot_reason = hsw_reboot_reason,
    .disable_c1e = true,
    .idle_prefer_hlt = false,
    .idle_states =
        {
            .states =
                {
                    // TODO(fxbug.dev/102663): fill this in.
                    X86_CSTATE_C1(0),
                },
            .default_state_mask = kX86IdleStateMaskC1Only,
        },
};

static const x86_microarch_config_t alder_lake_config{
    .x86_microarch = X86_MICROARCH_INTEL_ALDERLAKE,
    .get_apic_freq = default_apic_freq,
    .get_tsc_freq = intel_tsc_freq,
    .reboot_system = hsw_reboot_system,
    .reboot_reason = hsw_reboot_reason,
    .disable_c1e = true,
    .idle_prefer_hlt = false,
    .idle_states =
        {
            .states =
                {
                    // TODO(fxbug.dev/102663): fill this in.
                    X86_CSTATE_C1(0),
                },
            .default_state_mask = kX86IdleStateMaskC1Only,
        },
};

static const x86_microarch_config_t cannon_lake_config{
    .x86_microarch = X86_MICROARCH_INTEL_CANNONLAKE,
    .get_apic_freq = default_apic_freq,
    .get_tsc_freq = intel_tsc_freq,
    .reboot_system = hsw_reboot_system,
    .reboot_reason = hsw_reboot_reason,
    .disable_c1e = true,
    .idle_prefer_hlt = false,
    .idle_states =
        {
            .states =
                {// TODO: Read exit_latency from IRTL registers
                 {.name = "C6", .mwait_hint = 0x20, .exit_latency = 120, .flushes_tlb = true},
                 X86_CSTATE_C1(0)},
            .default_state_mask = kX86IdleStateMaskC1Only,
        },
};

static const x86_microarch_config_t skylake_config{
    .x86_microarch = X86_MICROARCH_INTEL_SKYLAKE,
    .get_apic_freq = skl_apic_freq,
    .get_tsc_freq = intel_tsc_freq,
    .reboot_system = hsw_reboot_system,
    .reboot_reason = hsw_reboot_reason,
    .disable_c1e = true,
    .idle_prefer_hlt = false,
    .idle_states =
        {
            .states =
                {
                    {.name = "C10", .mwait_hint = 0x60, .exit_latency = 890, .flushes_tlb = true},
                    {.name = "C9", .mwait_hint = 0x50, .exit_latency = 480, .flushes_tlb = true},
                    {.name = "C8", .mwait_hint = 0x40, .exit_latency = 200, .flushes_tlb = true},
                    {.name = "C7s", .mwait_hint = 0x33, .exit_latency = 124, .flushes_tlb = true},
                    {.name = "C6", .mwait_hint = 0x20, .exit_latency = 85, .flushes_tlb = true},
                    {.name = "C3", .mwait_hint = 0x10, .exit_latency = 70, .flushes_tlb = true},
                    X86_CSTATE_C1(0),
                },
            .default_state_mask = kX86IdleStateMaskC1Only,
        },
};

static const x86_microarch_config_t skylake_x_config{
    .x86_microarch = X86_MICROARCH_INTEL_SKYLAKE,
    .get_apic_freq = skl_x_apic_freq,
    .get_tsc_freq = intel_tsc_freq,
    .reboot_system = hsw_reboot_system,
    .reboot_reason = hsw_reboot_reason,
    .disable_c1e = true,
    .idle_prefer_hlt = false,
    .idle_states =
        {
            .states = {X86_CSTATE_C1(0)},
            .default_state_mask = kX86IdleStateMaskC1Only,
        },
};

static const x86_microarch_config_t broadwell_config{
    .x86_microarch = X86_MICROARCH_INTEL_BROADWELL,
    .get_apic_freq = bdw_apic_freq,
    .get_tsc_freq = intel_tsc_freq,
    .reboot_system = hsw_reboot_system,
    .reboot_reason = hsw_reboot_reason,
    .disable_c1e = true,
    .idle_prefer_hlt = false,
    .idle_states =
        {
            .states = {X86_CSTATE_C1(0)},
            .default_state_mask = kX86IdleStateMaskC1Only,
        },
};
static const x86_microarch_config_t haswell_config{
    .x86_microarch = X86_MICROARCH_INTEL_HASWELL,
    .get_apic_freq = bdw_apic_freq,
    .get_tsc_freq = intel_tsc_freq,
    .reboot_system = hsw_reboot_system,
    .reboot_reason = hsw_reboot_reason,
    .disable_c1e = true,
    .idle_prefer_hlt = false,
    .idle_states =
        {
            .states = {X86_CSTATE_C1(0)},
            .default_state_mask = kX86IdleStateMaskC1Only,
        },
};
static const x86_microarch_config_t ivybridge_config{
    .x86_microarch = X86_MICROARCH_INTEL_IVY_BRIDGE,
    .get_apic_freq = bdw_apic_freq,
    .get_tsc_freq = intel_tsc_freq,
    .reboot_system = unknown_reboot_system,
    .reboot_reason = unknown_reboot_reason,
    .disable_c1e = true,
    .idle_prefer_hlt = false,
    .idle_states =
        {
            .states = {X86_CSTATE_C1(0)},
            .default_state_mask = kX86IdleStateMaskC1Only,
        },
};
static const x86_microarch_config_t sandybridge_config{
    .x86_microarch = X86_MICROARCH_INTEL_SANDY_BRIDGE,
    .get_apic_freq = bdw_apic_freq,
    .get_tsc_freq = intel_tsc_freq,
    .reboot_system = unknown_reboot_system,
    .reboot_reason = unknown_reboot_reason,
    .disable_c1e = true,
    .idle_prefer_hlt = false,
    .idle_states =
        {
            .states = {X86_CSTATE_C1(0)},
            .default_state_mask = kX86IdleStateMaskC1Only,
        },
};
static const x86_microarch_config_t westmere_config{
    .x86_microarch = X86_MICROARCH_INTEL_WESTMERE,
    .get_apic_freq = default_apic_freq,
    .get_tsc_freq = intel_tsc_freq,
    .reboot_system = unknown_reboot_system,
    .reboot_reason = unknown_reboot_reason,
    .disable_c1e = true,
    .idle_prefer_hlt = false,
    .idle_states =
        {
            .states = {X86_CSTATE_C1(0)},
            .default_state_mask = kX86IdleStateMaskC1Only,
        },
};
static const x86_microarch_config_t nehalem_config{
    .x86_microarch = X86_MICROARCH_INTEL_NEHALEM,
    .get_apic_freq = default_apic_freq,
    .get_tsc_freq = intel_tsc_freq,
    .reboot_system = unknown_reboot_system,
    .reboot_reason = unknown_reboot_reason,
    .disable_c1e = true,
    .idle_prefer_hlt = false,
    .idle_states =
        {
            .states = {X86_CSTATE_C1(0)},
            .default_state_mask = kX86IdleStateMaskC1Only,
        },
};
static const x86_microarch_config_t silvermont_config{
    .x86_microarch = X86_MICROARCH_INTEL_SILVERMONT,
    .get_apic_freq = default_apic_freq,
    .get_tsc_freq = intel_tsc_freq,
    .reboot_system = unknown_reboot_system,
    .reboot_reason = unknown_reboot_reason,
    .disable_c1e = false,
    .idle_prefer_hlt = false,
    .idle_states =
        {
            .states = {X86_CSTATE_C1(0)},
            .default_state_mask = kX86IdleStateMaskC1Only,
        },
};
static const x86_microarch_config_t goldmont_config{
    .x86_microarch = X86_MICROARCH_INTEL_GOLDMONT,
    .get_apic_freq = default_apic_freq,
    .get_tsc_freq = intel_tsc_freq,
    .reboot_system = hsw_reboot_system,
    .reboot_reason = hsw_reboot_reason,
    .disable_c1e = false,
    // [APL30] Apollo Lake SOCs (Goldmont) have an errata which causes stores to not always wake
    // MWAIT-ing cores. Prefer HLT to avoid the issue.
    .idle_prefer_hlt = true,
    .idle_states =
        {
            .states = {X86_CSTATE_C1(0)},
            .default_state_mask = kX86IdleStateMaskC1Only,
        },
};
static const x86_microarch_config_t goldmont_plus_config{
    .x86_microarch = X86_MICROARCH_INTEL_GOLDMONT_PLUS,
    .get_apic_freq = default_apic_freq,
    .get_tsc_freq = intel_tsc_freq,
    .reboot_system = unknown_reboot_system,
    .reboot_reason = unknown_reboot_reason,
    .disable_c1e = false,
    .idle_prefer_hlt = false,
    .idle_states =
        {
            .states =
                {
                    // TODO(fxbug.dev/35457): Read C6 and deeper latency from IRTL registers
                    {.name = "C10", .mwait_hint = 0x60, .exit_latency = 10000, .flushes_tlb = true},
                    {.name = "C9", .mwait_hint = 0x50, .exit_latency = 2000, .flushes_tlb = true},
                    {.name = "C8", .mwait_hint = 0x40, .exit_latency = 1000, .flushes_tlb = true},
                    {.name = "C7s", .mwait_hint = 0x31, .exit_latency = 155, .flushes_tlb = true},
                    {.name = "C6", .mwait_hint = 0x20, .exit_latency = 133, .flushes_tlb = true},
                    {.name = "C1E", .mwait_hint = 0x01, .exit_latency = 10, .flushes_tlb = false},
                    X86_CSTATE_C1(0),
                },
            .default_state_mask = kX86IdleStateMaskC1Only,
        },
};
static const x86_microarch_config_t intel_default_config{
    .x86_microarch = X86_MICROARCH_UNKNOWN,
    .get_apic_freq = default_apic_freq,
    .get_tsc_freq = intel_tsc_freq,
    .reboot_system = unknown_reboot_system,
    .reboot_reason = unknown_reboot_reason,
    .disable_c1e = false,
    .idle_prefer_hlt = false,
    .idle_states =
        {
            .states = {X86_CSTATE_C1(0)},
            .default_state_mask = kX86IdleStateMaskC1Only,
        },
};

// AMD microarches
static const x86_microarch_config_t zen_config{
    .x86_microarch = X86_MICROARCH_AMD_ZEN,
    .get_apic_freq = unknown_freq,
    .get_tsc_freq = zen_tsc_freq,
    .reboot_system = unknown_reboot_system,
    .reboot_reason = unknown_reboot_reason,
    .disable_c1e = false,
    // Zen SOCs save substantial power using HLT instead of MWAIT.
    // TODO(fxbug.dev/61265): Use a predictor/selection to use mwait for short sleeps.
    .idle_prefer_hlt = true,
    .idle_states =
        {
            .states = {X86_CSTATE_C1(0)},
            .default_state_mask = kX86IdleStateMaskC1Only,
        },
};
static const x86_microarch_config_t jaguar_config{
    .x86_microarch = X86_MICROARCH_AMD_JAGUAR,
    .get_apic_freq = unknown_freq,
    .get_tsc_freq = unknown_freq,
    .reboot_system = unknown_reboot_system,
    .reboot_reason = unknown_reboot_reason,
    .disable_c1e = false,
    .idle_prefer_hlt = false,
    .idle_states =
        {
            .states = {X86_CSTATE_C1(0)},
            .default_state_mask = kX86IdleStateMaskC1Only,
        },
};
static const x86_microarch_config_t bulldozer_config{
    .x86_microarch = X86_MICROARCH_AMD_BULLDOZER,
    .get_apic_freq = bulldozer_apic_freq,
    .get_tsc_freq = unknown_freq,
    .reboot_system = unknown_reboot_system,
    .reboot_reason = unknown_reboot_reason,
    .disable_c1e = false,
    // Excavator SOCs in particular save substantial power using HLT instead of MWAIT
    .idle_prefer_hlt = true,
    .idle_states =
        {
            .states = {X86_CSTATE_C1(0)},
            .default_state_mask = kX86IdleStateMaskC1Only,
        },
};
static const x86_microarch_config_t amd_default_config{
    .x86_microarch = X86_MICROARCH_UNKNOWN,
    .get_apic_freq = unknown_freq,
    .get_tsc_freq = unknown_freq,
    .reboot_system = unknown_reboot_system,
    .reboot_reason = unknown_reboot_reason,
    .disable_c1e = false,
    .idle_prefer_hlt = false,
    .idle_states =
        {
            .states = {X86_CSTATE_C1(0)},
            .default_state_mask = kX86IdleStateMaskC1Only,
        },
};

// Unknown vendor config
static const x86_microarch_config_t unknown_vendor_config{
    .x86_microarch = X86_MICROARCH_UNKNOWN,
    .get_apic_freq = unknown_freq,
    .get_tsc_freq = unknown_freq,
    .reboot_system = unknown_reboot_system,
    .reboot_reason = unknown_reboot_reason,
    .disable_c1e = false,
    .idle_prefer_hlt = false,
    .idle_states =
        {
            .states = {X86_CSTATE_C1(0)},
            .default_state_mask = kX86IdleStateMaskC1Only,
        },
};

const x86_microarch_config_t* get_microarch_config(const cpu_id::CpuId* cpuid) {
  auto vendor = cpuid->ReadManufacturerInfo();
  auto processor_id = cpuid->ReadProcessorId();

  if (vendor.manufacturer() == cpu_id::ManufacturerInfo::INTEL && processor_id.family() == 0x6) {
    switch (processor_id.model()) {
      case 0x1a: /* Nehalem */
      case 0x1e: /* Nehalem */
      case 0x1f: /* Nehalem */
      case 0x2e: /* Nehalem */
        return &nehalem_config;
      case 0x25: /* Westmere */
      case 0x2c: /* Westmere */
      case 0x2f: /* Westmere */
        return &westmere_config;
      case 0x2a: /* Sandy Bridge */
      case 0x2d: /* Sandy Bridge EP */
        return &sandybridge_config;
      case 0x3a: /* Ivy Bridge */
      case 0x3e: /* Ivy Bridge EP */
        return &ivybridge_config;
      case 0x3c: /* Haswell DT */
      case 0x3f: /* Haswell MB */
      case 0x45: /* Haswell ULT */
      case 0x46: /* Haswell ULX */
        return &haswell_config;
      case 0x3d: /* Broadwell */
      case 0x47: /* Broadwell H */
      case 0x56: /* Broadwell EP */
      case 0x4f: /* Broadwell EX */
        return &broadwell_config;
      case 0x4e: /* Skylake Y/U */
      case 0x5e: /* Skylake H/S */
      case 0x8e: /* Kaby Lake Y/U, Coffee Lake, Whiskey Lake */
      case 0x9e: /* Kaby Lake H/S, Coffee Lake, Whiskey Lake */
        return &skylake_config;
      case 0x55: /* Skylake X/SP, Cascade Lake */
        return &skylake_x_config;
      case 0x66: /* Cannon Lake U */
        return &cannon_lake_config;
      case 0x6a: /* Ice Lake-SP */
        return &icelake_config;
      case 0x8c: /* Tiger Lake UP */
      case 0x8d: /* Tiger Lake H */
        return &tiger_lake_config;
      case 0x97: /* Alder Lake S */
      case 0x9a: /* Alder Lake H/P/U */
        return &alder_lake_config;
      case 0x37: /* Silvermont */
      case 0x4a: /* Silvermont "Cherry View" */
      case 0x4d: /* Silvermont "Avoton" */
      case 0x4c: /* Airmont "Braswell" */
      case 0x5a: /* Airmont */
        return &silvermont_config;
      case 0x5c: /* Goldmont (Apollo Lake) */
      case 0x5f: /* Goldmont (Denverton) */
        return &goldmont_config;
      case 0x7a: /* Goldmont Plus (Gemini Lake) */
        return &goldmont_plus_config;
      default:
        return &intel_default_config;
    }
  } else if (vendor.manufacturer() == cpu_id::ManufacturerInfo::AMD) {
    switch (processor_id.family()) {
      case 0x15:
        return &bulldozer_config;
      case 0x16:
        return &jaguar_config;
      case 0x17: /* Zen 1, 2 */
      case 0x19: /* Zen 3, 4 */
        return &zen_config;
      default:
        return &amd_default_config;
    }
  }

  return &unknown_vendor_config;
}

extern "C" {

void x86_cpu_maybe_l1d_flush(zx_status_t syscall_return) {
  if (gBootOptions->x86_disable_spec_mitigations) {
    return;
  }

  // Spectre V1: If we are returning from a syscall with one of these errors, flush the entire
  // L1D cache. This prevents hostile code from reading any data the kernel brought in to cache,
  // even speculatively.
  //
  // We only flush on these errors as they are not expected in the steady state and cover most
  // expected Spectre V1 attack constructions. Most attacks will either pass in invalid indexes
  // or invalid handles, to leak table contents; ZX_ERR_INVALID_ARGS and ZX_ERR_BAD_HANDLE cover
  // those cases.
  //
  // Allowing a process to cause an L1D cache flush is low risk; the process could cycle enough
  // data through the L1 to evict + replace all data very quickly. Allowing a process to cause
  // a WBINVD, however, would be higher-risk - it flushes every cache in the system, which could
  // be very disruptive to other work; therefore we don't fall back from IA32_FLUSH_CMD to WBINVD.
  if (syscall_return == ZX_ERR_INVALID_ARGS || syscall_return == ZX_ERR_BAD_HANDLE) {
    if (x86_feature_test(X86_FEATURE_L1D_FLUSH)) {
      write_msr(X86_MSR_IA32_FLUSH_CMD, 1);
    }
  }
}
}
