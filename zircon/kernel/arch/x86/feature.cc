// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT
#include "arch/x86/feature.h"

#include <assert.h>
#include <bits.h>
#include <stdint.h>
#include <string.h>
#include <trace.h>

#include <arch/ops.h>
#include <arch/x86/cpuid.h>
#include <arch/x86/mmu.h>
#include <arch/x86/platform_access.h>
#include <fbl/algorithm.h>
#include <ktl/atomic.h>
#include <lib/cmdline.h>
#include <lib/code_patching.h>
#include <platform/pc/bootbyte.h>

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
bool g_swapgs_bug_mitigated;
bool g_has_ssb;
bool g_has_ssbd;
bool g_ssb_mitigated;
bool g_has_md_clear;
bool g_md_clear_on_user_return;
bool g_has_ibpb;
bool g_should_ibpb_on_ctxt_switch;
bool g_ras_fill_on_entry;
// True if we should disable all speculative execution mitigations.
bool g_disable_spec_mitigations;

enum x86_hypervisor_list x86_hypervisor;

static ktl::atomic<bool> g_cpuid_initialized;

static enum x86_hypervisor_list get_hypervisor();

void x86_feature_init(void) {
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

  g_x86_feature_fsgsbase = x86_feature_test(X86_FEATURE_FSGSBASE);
  g_x86_feature_pcid_good =
      x86_feature_test(X86_FEATURE_PCID) && x86_feature_test(X86_FEATURE_INVPCID);

  x86_hypervisor = get_hypervisor();

  MsrAccess msr;
  g_disable_spec_mitigations = gCmdline.GetBool("kernel.x86.disable_spec_mitigations",
                                                /*default_value=*/false);
  if (x86_vendor == X86_VENDOR_INTEL) {
    g_has_meltdown = x86_intel_cpu_has_meltdown(&cpuid, &msr);
    g_has_l1tf = x86_intel_cpu_has_l1tf(&cpuid, &msr);
    g_l1d_flush_on_vmentry = ((x86_get_disable_spec_mitigations() == false)) &&
                             g_has_l1tf && x86_feature_test(X86_FEATURE_L1D_FLUSH);
    g_has_mds_taa = x86_intel_cpu_has_mds_taa(&cpuid, &msr);
    g_has_md_clear = cpuid.ReadFeatures().HasFeature(cpu_id::Features::MD_CLEAR);
    g_md_clear_on_user_return = ((x86_get_disable_spec_mitigations() == false)) &&
                                g_has_mds_taa && g_has_md_clear &&
                                gCmdline.GetBool("kernel.x86.md_clear_on_user_return",
                                                 /*default_value=*/true);
    g_has_swapgs_bug = x86_intel_cpu_has_swapgs_bug(&cpuid);
    g_has_ssb = x86_intel_cpu_has_ssb(&cpuid, &msr);
    g_has_ssbd = x86_intel_cpu_has_ssbd(&cpuid, &msr);
    g_has_ibpb = cpuid.ReadFeatures().HasFeature(cpu_id::Features::SPEC_CTRL);
  } else if (x86_vendor == X86_VENDOR_AMD) {
    g_has_ssb = x86_amd_cpu_has_ssb(&cpuid, &msr);
    g_has_ssbd = x86_amd_cpu_has_ssbd(&cpuid, &msr);
    g_has_ibpb = cpuid.ReadFeatures().HasFeature(cpu_id::Features::AMD_IBPB);
  }
  // TODO(fxb/33667, fxb/12150): Consider whether a process can opt-out of an IBPB on switch,
  // either on switch-in (ex: its compiled with a retpoline) or switch-out (ex: it promises
  // not to attack the next process).
  // TODO(fxb/33667, fxb/12150): Should we have an individual knob for IBPB?
  g_should_ibpb_on_ctxt_switch = (x86_get_disable_spec_mitigations() == false) && g_has_ibpb;

  g_ssb_mitigated = (x86_get_disable_spec_mitigations() == false) && g_has_ssb && g_has_ssbd &&
                    gCmdline.GetBool("kernel.x86.spec_store_bypass_disable",
                                     /*default_value=*/false);
  g_x86_feature_has_smap = x86_feature_test(X86_FEATURE_SMAP);
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

bool x86_topology_enumerate(uint8_t level, struct x86_topology_level* info) {
  DEBUG_ASSERT(info);

  uint32_t eax, ebx, ecx, edx;
  cpuid_c(X86_CPUID_TOPOLOGY, level, &eax, &ebx, &ecx, &edx);

  uint8_t type = (ecx >> 8) & 0xff;
  if (type == X86_TOPOLOGY_INVALID) {
    return false;
  }

  info->right_shift = eax & 0x1f;
  info->type = type;
  return true;
}

const struct x86_model_info* x86_get_model(void) { return &model_info; }

void x86_feature_debug(void) {
  const struct {
    struct x86_cpuid_bit bit;
    const char* name;
  } features[] = {
      {X86_FEATURE_FPU, "fpu"},
      {X86_FEATURE_SSE, "sse"},
      {X86_FEATURE_SSE2, "sse2"},
      {X86_FEATURE_SSE3, "sse3"},
      {X86_FEATURE_SSSE3, "ssse3"},
      {X86_FEATURE_SSE4_1, "sse4.1"},
      {X86_FEATURE_SSE4_2, "sse4.2"},
      {X86_FEATURE_MMX, "mmx"},
      {X86_FEATURE_AVX, "avx"},
      {X86_FEATURE_AVX2, "avx2"},
      {X86_FEATURE_FXSR, "fxsr"},
      {X86_FEATURE_PCID, "pcid"},
      {X86_FEATURE_XSAVE, "xsave"},
      {X86_FEATURE_MON, "mon"},
      {X86_FEATURE_AESNI, "aesni"},
      {X86_FEATURE_CLFLUSH, "clflush"},
      {X86_FEATURE_CLFLUSHOPT, "clflushopt"},
      {X86_FEATURE_CLWB, "clwb"},
      {X86_FEATURE_FSGSBASE, "fsgsbase"},
      {X86_FEATURE_TSC_ADJUST, "tsc_adj"},
      {X86_FEATURE_SMEP, "smep"},
      {X86_FEATURE_SMAP, "smap"},
      {X86_FEATURE_ERMS, "erms"},
      {X86_FEATURE_RDRAND, "rdrand"},
      {X86_FEATURE_RDSEED, "rdseed"},
      {X86_FEATURE_UMIP, "umip"},
      {X86_FEATURE_PKU, "pku"},
      {X86_FEATURE_SYSCALL, "syscall"},
      {X86_FEATURE_NX, "nx"},
      {X86_FEATURE_HUGE_PAGE, "huge"},
      {X86_FEATURE_RDTSCP, "rdtscp"},
      {X86_FEATURE_INVAR_TSC, "invar_tsc"},
      {X86_FEATURE_TSC_DEADLINE, "tsc_deadline"},
      {X86_FEATURE_X2APIC, "x2apic"},
      {X86_FEATURE_VMX, "vmx"},
      {X86_FEATURE_HYPERVISOR, "hypervisor"},
      {X86_FEATURE_PT, "pt"},
      {X86_FEATURE_HWP, "hwp"},
  };

  const char* vendor_string = nullptr;
  switch (x86_vendor) {
    case X86_VENDOR_UNKNOWN:
      vendor_string = "unknown";
      break;
    case X86_VENDOR_INTEL:
      vendor_string = "Intel";
      break;
    case X86_VENDOR_AMD:
      vendor_string = "AMD";
      break;
  }
  printf("Vendor: %s\n", vendor_string);

  const char* microarch_string = nullptr;
  switch (x86_microarch_config->x86_microarch) {
    case X86_MICROARCH_UNKNOWN:
      microarch_string = "unknown";
      break;
    case X86_MICROARCH_INTEL_NEHALEM:
      microarch_string = "Nehalem";
      break;
    case X86_MICROARCH_INTEL_WESTMERE:
      microarch_string = "Westmere";
      break;
    case X86_MICROARCH_INTEL_SANDY_BRIDGE:
      microarch_string = "Sandy Bridge";
      break;
    case X86_MICROARCH_INTEL_IVY_BRIDGE:
      microarch_string = "Ivy Bridge";
      break;
    case X86_MICROARCH_INTEL_BROADWELL:
      microarch_string = "Broadwell";
      break;
    case X86_MICROARCH_INTEL_HASWELL:
      microarch_string = "Haswell";
      break;
    case X86_MICROARCH_INTEL_SKYLAKE:
      microarch_string = "Skylake";
      break;
    case X86_MICROARCH_INTEL_KABYLAKE:
      microarch_string = "Kaby Lake";
      break;
    case X86_MICROARCH_INTEL_SILVERMONT:
      microarch_string = "Silvermont";
      break;
    case X86_MICROARCH_INTEL_GOLDMONT:
      microarch_string = "Goldmont";
      break;
    case X86_MICROARCH_AMD_BULLDOZER:
      microarch_string = "Bulldozer";
      break;
    case X86_MICROARCH_AMD_JAGUAR:
      microarch_string = "Jaguar";
      break;
    case X86_MICROARCH_AMD_ZEN:
      microarch_string = "Zen";
      break;
  }
  printf("Microarch: %s\n", microarch_string);
  printf("F/M/S: %x/%x/%x\n", model_info.display_family, model_info.display_model,
         model_info.stepping);
  printf("patch_level: %x\n", model_info.patch_level);

  char brand_string[50];
  memset(brand_string, 0, sizeof(brand_string));
  const struct cpuid_leaf* leaf;
  uint32_t leaf_num = X86_CPUID_BRAND;
  for (int i = 0; i < 3; i++) {
    leaf = x86_get_cpuid_leaf((enum x86_cpuid_leaf_num)(leaf_num + i));
    if (!leaf) {
      break;
    }
    memcpy(brand_string + (i * 16), &leaf->a, sizeof(uint32_t));
    memcpy(brand_string + (i * 16) + 4, &leaf->b, sizeof(uint32_t));
    memcpy(brand_string + (i * 16) + 8, &leaf->c, sizeof(uint32_t));
    memcpy(brand_string + (i * 16) + 12, &leaf->d, sizeof(uint32_t));
  }
  printf("Brand: %s\n", brand_string);

  printf("Features: ");
  uint col = 0;
  for (uint i = 0; i < fbl::count_of(features); ++i) {
    if (x86_feature_test(features[i].bit))
      col += printf("%s ", features[i].name);
    if (col >= 80) {
      printf("\n");
      col = 0;
    }
  }
  if (col > 0)
    printf("\n");
  // Print synthetic 'features'/properties
  printf("Properties: ");
  if (g_has_meltdown)
    printf("meltdown ");
  if (g_has_l1tf)
    printf("l1tf ");
  if (g_has_mds_taa)
    printf("mds/taa ");
  if (g_has_md_clear)
    printf("md_clear ");
  if (g_md_clear_on_user_return)
    printf("md_clear_user_return ");
  if (g_has_swapgs_bug)
    printf("swapgs_bug ");
  if (g_swapgs_bug_mitigated)
    printf("swapgs_bug_mitigated ");
  if (g_x86_feature_pcid_good)
    printf("pcid_good ");
  if (x86_kpti_is_enabled()) {
    printf("pti_enabled ");
  }
  if (g_has_ssb)
    printf("ssb ");
  if (g_has_ssbd)
    printf("ssbd ");
  if (g_ssb_mitigated)
    printf("ssb_mitigated ");
  if (g_has_ibpb)
    printf("ibpb ");
  if (g_l1d_flush_on_vmentry)
    printf("l1d_flush_on_vmentry ");
  if (g_should_ibpb_on_ctxt_switch)
    printf("ibpb_ctxt_switch ");
  if (g_ras_fill_on_entry)
    printf("ras_fill ");
  printf("\n");
}

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

static uint64_t kbl_apic_freq() {
  uint64_t v = default_apic_freq();
  if (v != 0) {
    return v;
  }
  return 24ul * 1000 * 1000;
}

static uint64_t bdw_apic_freq() {
  uint64_t v = default_apic_freq();
  if (v != 0) {
    return v;
  }
  uint64_t platform_info;
  const uint32_t msr_platform_info = 0xce;
  if (read_msr_safe(msr_platform_info, &platform_info) == ZX_OK) {
    uint64_t bus_freq_mult = (platform_info >> 8) & 0xf;
    return bus_freq_mult * 100 * 1000 * 1000;
  }
  return 0;
}

static uint64_t bulldozer_apic_freq() {
  uint64_t v = default_apic_freq();
  if (v != 0) {
    return v;
  }

  // 15h-17h BKDGs mention the APIC timer rate is 2xCLKIN,
  // which experimentally appears to be 100Mhz always
  return 100ul * 1000 * 1000;
}

static uint64_t unknown_freq() { return 0; }

static uint64_t intel_tsc_freq() {
  const uint64_t core_crystal_clock_freq = x86_get_microarch_config()->get_apic_freq();

  // If this leaf is present, then 18.18.3 (Determining the Processor Base
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
static const x86_microarch_config_t kbl_config{
    .x86_microarch = X86_MICROARCH_INTEL_KABYLAKE,
    .get_apic_freq = kbl_apic_freq,
    .get_tsc_freq = intel_tsc_freq,
    .reboot_system = hsw_reboot_system,
    .reboot_reason = hsw_reboot_reason,
    .disable_c1e = true,
    .has_meltdown = true,
    .has_l1tf = true,
    .has_mds = true,
    .has_swapgs_bug = true,
    .has_ssb = true,
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
static const x86_microarch_config_t skl_config{
    .x86_microarch = X86_MICROARCH_INTEL_SKYLAKE,
    .get_apic_freq = kbl_apic_freq,
    .get_tsc_freq = intel_tsc_freq,
    .reboot_system = hsw_reboot_system,
    .reboot_reason = hsw_reboot_reason,
    .disable_c1e = true,
    .has_meltdown = true,
    .has_l1tf = true,
    .has_mds = true,
    .has_swapgs_bug = true,
    .has_ssb = true,
    .idle_states =
        {
            .states = {X86_CSTATE_C1(0)},
            .default_state_mask = kX86IdleStateMaskC1Only,
        },
};
static const x86_microarch_config_t bdw_config{
    .x86_microarch = X86_MICROARCH_INTEL_BROADWELL,
    .get_apic_freq = bdw_apic_freq,
    .get_tsc_freq = intel_tsc_freq,
    .reboot_system = hsw_reboot_system,
    .reboot_reason = hsw_reboot_reason,
    .disable_c1e = true,
    .has_meltdown = true,
    .has_l1tf = true,
    .has_mds = true,
    .has_swapgs_bug = true,
    .has_ssb = true,
    .idle_states =
        {
            .states = {X86_CSTATE_C1(0)},
            .default_state_mask = kX86IdleStateMaskC1Only,
        },
};
static const x86_microarch_config_t hsw_config{
    .x86_microarch = X86_MICROARCH_INTEL_HASWELL,
    .get_apic_freq = bdw_apic_freq,
    .get_tsc_freq = intel_tsc_freq,
    .reboot_system = hsw_reboot_system,
    .reboot_reason = hsw_reboot_reason,
    .disable_c1e = true,
    .has_meltdown = true,
    .has_l1tf = true,
    .has_mds = true,
    .has_swapgs_bug = true,
    .has_ssb = true,
    .idle_states =
        {
            .states = {X86_CSTATE_C1(0)},
            .default_state_mask = kX86IdleStateMaskC1Only,
        },
};
static const x86_microarch_config_t ivb_config{
    .x86_microarch = X86_MICROARCH_INTEL_IVY_BRIDGE,
    .get_apic_freq = bdw_apic_freq,
    .get_tsc_freq = intel_tsc_freq,
    .reboot_system = unknown_reboot_system,
    .reboot_reason = unknown_reboot_reason,
    .disable_c1e = true,
    .has_meltdown = true,
    .has_l1tf = true,
    .has_mds = true,
    .has_swapgs_bug = true,
    .has_ssb = true,
    .idle_states =
        {
            .states = {X86_CSTATE_C1(0)},
            .default_state_mask = kX86IdleStateMaskC1Only,
        },
};
static const x86_microarch_config_t snb_config{
    .x86_microarch = X86_MICROARCH_INTEL_SANDY_BRIDGE,
    .get_apic_freq = bdw_apic_freq,
    .get_tsc_freq = intel_tsc_freq,
    .reboot_system = unknown_reboot_system,
    .reboot_reason = unknown_reboot_reason,
    .disable_c1e = true,
    .has_meltdown = true,
    .has_l1tf = true,
    .has_mds = true,
    .has_swapgs_bug = true,
    .has_ssb = true,
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
    .has_meltdown = true,
    .has_l1tf = true,
    .has_mds = true,
    .has_swapgs_bug = true,
    .has_ssb = true,
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
    .has_meltdown = true,
    .has_l1tf = true,
    .has_mds = true,
    .has_swapgs_bug = true,
    .has_ssb = true,
    .idle_states =
        {
            .states = {X86_CSTATE_C1(0)},
            .default_state_mask = kX86IdleStateMaskC1Only,
        },
};
static const x86_microarch_config_t smt_config{
    .x86_microarch = X86_MICROARCH_INTEL_SILVERMONT,
    .get_apic_freq = default_apic_freq,
    .get_tsc_freq = intel_tsc_freq,
    .reboot_system = unknown_reboot_system,
    .reboot_reason = unknown_reboot_reason,
    .disable_c1e = false,
    .has_meltdown = false,
    .has_l1tf = false,
    .has_mds = true,
    .has_swapgs_bug = false,
    .has_ssb = false,
    .idle_states =
        {
            .states = {X86_CSTATE_C1(0)},
            .default_state_mask = kX86IdleStateMaskC1Only,
        },
};
static const x86_microarch_config_t glm_config{
    // Goldmont, Goldmont+
    .x86_microarch = X86_MICROARCH_INTEL_GOLDMONT,
    .get_apic_freq = default_apic_freq,
    .get_tsc_freq = intel_tsc_freq,
    .reboot_system = unknown_reboot_system,
    .reboot_reason = unknown_reboot_reason,
    .disable_c1e = false,
    .has_meltdown = true,
    .has_l1tf = false,
    .has_mds = false,
    .has_swapgs_bug = false,
    .has_ssb = true,
    .idle_states =
        {
            .states = {X86_CSTATE_C1(0)},
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
    .has_meltdown = true,
    .has_l1tf = true,
    .has_mds = true,
    .has_swapgs_bug = true,
    .has_ssb = true,
    .idle_states =
        {
            .states = {X86_CSTATE_C1(0)},
            .default_state_mask = kX86IdleStateMaskC1Only,
        },
};

// AMD microarches
static const x86_microarch_config_t zen_config{
    .x86_microarch = X86_MICROARCH_AMD_ZEN,
    .get_apic_freq = bulldozer_apic_freq,
    .get_tsc_freq = zen_tsc_freq,
    .reboot_system = unknown_reboot_system,
    .reboot_reason = unknown_reboot_reason,
    .disable_c1e = false,
    .has_meltdown = false,
    .has_l1tf = false,
    .has_mds = false,
    .has_swapgs_bug = false,
    .has_ssb = true,
    .idle_states =
        {
            .states = {X86_CSTATE_C1(0)},
            .default_state_mask = kX86IdleStateMaskC1Only,
        },
};
static const x86_microarch_config_t jaguar_config{
    .x86_microarch = X86_MICROARCH_AMD_JAGUAR,
    .get_apic_freq = bulldozer_apic_freq,
    .get_tsc_freq = unknown_freq,
    .reboot_system = unknown_reboot_system,
    .reboot_reason = unknown_reboot_reason,
    .disable_c1e = false,
    .has_meltdown = false,
    .has_l1tf = false,
    .has_mds = false,
    .has_swapgs_bug = false,
    .has_ssb = true,
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
    .has_meltdown = false,
    .has_l1tf = false,
    .has_mds = false,
    .has_swapgs_bug = false,
    .has_ssb = true,
    .idle_states =
        {
            .states = {X86_CSTATE_C1(0)},
            .default_state_mask = kX86IdleStateMaskC1Only,
        },
};
static const x86_microarch_config_t amd_default_config{
    .x86_microarch = X86_MICROARCH_UNKNOWN,
    .get_apic_freq = default_apic_freq,
    .get_tsc_freq = unknown_freq,
    .reboot_system = unknown_reboot_system,
    .reboot_reason = unknown_reboot_reason,
    .disable_c1e = false,
    .has_meltdown = false,
    .has_l1tf = false,
    .has_mds = false,
    .has_swapgs_bug = false,
    .has_ssb = true,
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
    .has_meltdown = true,
    .has_l1tf = true,
    .has_mds = true,
    .has_swapgs_bug = false,
    .has_ssb = true,
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
        return &snb_config;
      case 0x3a: /* Ivy Bridge */
      case 0x3e: /* Ivy Bridge EP */
        return &ivb_config;
      case 0x3c: /* Haswell DT */
      case 0x3f: /* Haswell MB */
      case 0x45: /* Haswell ULT */
      case 0x46: /* Haswell ULX */
        return &hsw_config;
      case 0x3d: /* Broadwell */
      case 0x47: /* Broadwell H */
      case 0x56: /* Broadwell EP */
      case 0x4f: /* Broadwell EX */
        return &bdw_config;
      case 0x4e: /* Skylake Y/U */
      case 0x5e: /* Skylake H/S */
      case 0x55: /* Skylake E */
        return &skl_config;
      case 0x8e: /* Kabylake Y/U */
      case 0x9e: /* Kabylake H/S */
        return &kbl_config;
      case 0x37: /* Silvermont */
      case 0x4a: /* Silvermont "Cherry View" */
      case 0x4d: /* Silvermont "Avoton" */
      case 0x4c: /* Airmont "Braswell" */
      case 0x5a: /* Airmont */
        return &smt_config;
      case 0x5c: /* Goldmont (Apollo Lake) */
      case 0x5f: /* Goldmont (Denverton) */
      case 0x7a: /* Goldmont+ (Gemini Lake) */
        return &glm_config;
      default:
        return &intel_default_config;
    }
  } else if (vendor.manufacturer() == cpu_id::ManufacturerInfo::AMD) {
    switch (processor_id.family()) {
      case 0x15:
        return &bulldozer_config;
      case 0x16:
        return &jaguar_config;
      case 0x17:
        return &zen_config;
      default:
        return &amd_default_config;
    }
  }

  return &unknown_vendor_config;
}

extern "C" {

void x86_cpu_ibpb(MsrAccess* msr) {
  msr->write_msr(/*msr_index=*/X86_MSR_IA32_PRED_CMD, /*value=*/1);
}

const uint8_t kNop = 0x90;
CODE_TEMPLATE(kLfence, "lfence")
void swapgs_bug_postfence(const CodePatchInfo* patch) {
  const size_t kSize = 3;
  DEBUG_ASSERT(patch->dest_size == kSize);
  DEBUG_ASSERT(kLfenceEnd - kLfence == kSize);

  if ((x86_get_disable_spec_mitigations() == false) && g_has_swapgs_bug) {
    memcpy(patch->dest_addr, kLfence, kSize);
    g_swapgs_bug_mitigated = true;
  } else {
    memset(patch->dest_addr, kNop, kSize);
  }
}

extern "C" void x86_ras_fill_select(const CodePatchInfo* patch) {
  const size_t kSize = 9;
  DEBUG_ASSERT(patch->dest_size == kSize);

  // TODO(fxb/33667): Consider whether Enhanced IBRS + SMEP + SMAP requires filling RAS.
  if (g_disable_spec_mitigations) {
     memset(patch->dest_addr, kNop, kSize);
  } else {
     // Keep the call to x86_ras_fill in place.
     g_ras_fill_on_entry = true;
  }
}

}
