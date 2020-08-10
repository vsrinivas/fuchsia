// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#ifndef ZIRCON_KERNEL_ARCH_X86_INCLUDE_ARCH_X86_FEATURE_H_
#define ZIRCON_KERNEL_ARCH_X86_INCLUDE_ARCH_X86_FEATURE_H_

#include <assert.h>
#include <stdint.h>
#include <zircon/compiler.h>

#include <arch/x86.h>
#include <arch/x86/idle_states.h>

#ifdef __cplusplus

namespace cpu_id {
class CpuId;
}  // namespace cpu_id

class MsrAccess;

#endif  // __cplusplus

__BEGIN_CDECLS

#define MAX_SUPPORTED_CPUID (0x17)
#define MAX_SUPPORTED_CPUID_HYP (0x40000001)
#define MAX_SUPPORTED_CPUID_EXT (0x8000001e)

struct cpuid_leaf {
  uint32_t a;
  uint32_t b;
  uint32_t c;
  uint32_t d;
};

enum x86_cpuid_leaf_num {
  X86_CPUID_BASE = 0,
  X86_CPUID_MODEL_FEATURES = 0x1,
  X86_CPUID_CACHE_V1 = 0x2,
  X86_CPUID_CACHE_V2 = 0x4,
  X86_CPUID_MON = 0x5,
  X86_CPUID_THERMAL_AND_POWER = 0x6,
  X86_CPUID_EXTENDED_FEATURE_FLAGS = 0x7,
  X86_CPUID_PERFORMANCE_MONITORING = 0xa,
  X86_CPUID_TOPOLOGY = 0xb,
  X86_CPUID_XSAVE = 0xd,
  X86_CPUID_PT = 0x14,
  X86_CPUID_TSC = 0x15,

  X86_CPUID_HYP_BASE = 0x40000000,
  X86_CPUID_HYP_VENDOR = 0x40000000,
  X86_CPUID_KVM_FEATURES = 0x40000001,

  X86_CPUID_EXT_BASE = 0x80000000,
  X86_CPUID_BRAND = 0x80000002,
  X86_CPUID_ADDR_WIDTH = 0x80000008,
  X86_CPUID_AMD_TOPOLOGY = 0x8000001e,
};

struct x86_cpuid_bit {
  enum x86_cpuid_leaf_num leaf_num;
  uint8_t word;
  uint8_t bit;
};

#define X86_CPUID_BIT(leaf, word, bit) \
  (struct x86_cpuid_bit) { (enum x86_cpuid_leaf_num)(leaf), (word), (bit) }

/* Invoked on each CPU prior to lk_main being called. */
void x86_feature_early_init_percpu(void);

/* Invoked on boot CPU after command line and UART enabled, but before
 * code patching or the MMU are enabled. */
void x86_cpu_feature_init(void);

/* Invoked on each CPU late in init sequence. */
void x86_cpu_feature_late_init_percpu(void);

static inline const struct cpuid_leaf* x86_get_cpuid_leaf(enum x86_cpuid_leaf_num leaf) {
  extern struct cpuid_leaf _cpuid[MAX_SUPPORTED_CPUID + 1];
  extern struct cpuid_leaf _cpuid_hyp[MAX_SUPPORTED_CPUID_HYP - X86_CPUID_HYP_BASE + 1];
  extern struct cpuid_leaf _cpuid_ext[MAX_SUPPORTED_CPUID_EXT - X86_CPUID_EXT_BASE + 1];
  extern uint32_t max_cpuid;
  extern uint32_t max_ext_cpuid;
  extern uint32_t max_hyp_cpuid;

  if (leaf < X86_CPUID_HYP_BASE) {
    if (unlikely(leaf > max_cpuid))
      return NULL;

    return &_cpuid[leaf];
  } else if (leaf < X86_CPUID_EXT_BASE) {
    if (unlikely(leaf > max_hyp_cpuid))
      return NULL;

    return &_cpuid_hyp[(uint32_t)leaf - (uint32_t)X86_CPUID_HYP_BASE];
  } else {
    if (unlikely(leaf > max_ext_cpuid))
      return NULL;

    return &_cpuid_ext[(uint32_t)leaf - (uint32_t)X86_CPUID_EXT_BASE];
  }
}
/* Retrieve the specified subleaf.  This function is not cached.
 * Returns false if leaf num is invalid */
bool x86_get_cpuid_subleaf(enum x86_cpuid_leaf_num, uint32_t, struct cpuid_leaf*);

static inline bool x86_feature_test(struct x86_cpuid_bit bit) {
  DEBUG_ASSERT(bit.word <= 3 && bit.bit <= 31);

  if (bit.word > 3 || bit.bit > 31)
    return false;

  const struct cpuid_leaf* leaf = x86_get_cpuid_leaf(bit.leaf_num);
  if (!leaf)
    return false;

  switch (bit.word) {
    case 0:
      return !!((1u << bit.bit) & leaf->a);
    case 1:
      return !!((1u << bit.bit) & leaf->b);
    case 2:
      return !!((1u << bit.bit) & leaf->c);
    case 3:
      return !!((1u << bit.bit) & leaf->d);
    default:
      return false;
  }
}

void x86_feature_debug(void);

/* add feature bits to test here */
/* format: X86_CPUID_BIT(cpuid leaf, register (eax-edx:0-3), bit) */
#define X86_FEATURE_SSE3 X86_CPUID_BIT(0x1, 2, 0)
#define X86_FEATURE_MON X86_CPUID_BIT(0x1, 2, 3)
#define X86_FEATURE_VMX X86_CPUID_BIT(0x1, 2, 5)
#define X86_FEATURE_TM2 X86_CPUID_BIT(0x1, 2, 8)
#define X86_FEATURE_SSSE3 X86_CPUID_BIT(0x1, 2, 9)
#define X86_FEATURE_PDCM X86_CPUID_BIT(0x1, 2, 15)
#define X86_FEATURE_PCID X86_CPUID_BIT(0x1, 2, 17)
#define X86_FEATURE_SSE4_1 X86_CPUID_BIT(0x1, 2, 19)
#define X86_FEATURE_SSE4_2 X86_CPUID_BIT(0x1, 2, 20)
#define X86_FEATURE_X2APIC X86_CPUID_BIT(0x1, 2, 21)
#define X86_FEATURE_TSC_DEADLINE X86_CPUID_BIT(0x1, 2, 24)
#define X86_FEATURE_AESNI X86_CPUID_BIT(0x1, 2, 25)
#define X86_FEATURE_XSAVE X86_CPUID_BIT(0x1, 2, 26)
#define X86_FEATURE_AVX X86_CPUID_BIT(0x1, 2, 28)
#define X86_FEATURE_RDRAND X86_CPUID_BIT(0x1, 2, 30)
#define X86_FEATURE_HYPERVISOR X86_CPUID_BIT(0x1, 2, 31)
#define X86_FEATURE_FPU X86_CPUID_BIT(0x1, 3, 0)
#define X86_FEATURE_SEP X86_CPUID_BIT(0x1, 3, 11)
#define X86_FEATURE_CLFLUSH X86_CPUID_BIT(0x1, 3, 19)
#define X86_FEATURE_ACPI X86_CPUID_BIT(0x1, 3, 22)
#define X86_FEATURE_MMX X86_CPUID_BIT(0x1, 3, 23)
#define X86_FEATURE_FXSR X86_CPUID_BIT(0x1, 3, 24)
#define X86_FEATURE_SSE X86_CPUID_BIT(0x1, 3, 25)
#define X86_FEATURE_SSE2 X86_CPUID_BIT(0x1, 3, 26)
#define X86_FEATURE_TM X86_CPUID_BIT(0x1, 3, 29)
#define X86_FEATURE_DTS X86_CPUID_BIT(0x6, 0, 0)
#define X86_FEATURE_PLN X86_CPUID_BIT(0x6, 0, 4)
#define X86_FEATURE_PTM X86_CPUID_BIT(0x6, 0, 6)
#define X86_FEATURE_HWP X86_CPUID_BIT(0x6, 0, 7)
#define X86_FEATURE_HWP_NOT X86_CPUID_BIT(0x6, 0, 8)
#define X86_FEATURE_HWP_ACT X86_CPUID_BIT(0x6, 0, 9)
#define X86_FEATURE_HWP_PREF X86_CPUID_BIT(0x6, 0, 10)
#define X86_FEATURE_HW_FEEDBACK X86_CPUID_BIT(0x6, 2, 0)
#define X86_FEATURE_PERF_BIAS X86_CPUID_BIT(0x6, 2, 3)
#define X86_FEATURE_FSGSBASE X86_CPUID_BIT(0x7, 1, 0)
#define X86_FEATURE_TSC_ADJUST X86_CPUID_BIT(0x7, 1, 1)
#define X86_FEATURE_AVX2 X86_CPUID_BIT(0x7, 1, 5)
#define X86_FEATURE_SMEP X86_CPUID_BIT(0x7, 1, 7)
#define X86_FEATURE_ERMS X86_CPUID_BIT(0x7, 1, 9)
#define X86_FEATURE_INVPCID X86_CPUID_BIT(0x7, 1, 10)
#define X86_FEATURE_RDSEED X86_CPUID_BIT(0x7, 1, 18)
#define X86_FEATURE_SMAP X86_CPUID_BIT(0x7, 1, 20)
#define X86_FEATURE_CLFLUSHOPT X86_CPUID_BIT(0x7, 1, 23)
#define X86_FEATURE_CLWB X86_CPUID_BIT(0x7, 1, 24)
#define X86_FEATURE_PT X86_CPUID_BIT(0x7, 1, 25)
#define X86_FEATURE_UMIP X86_CPUID_BIT(0x7, 2, 2)
#define X86_FEATURE_PKU X86_CPUID_BIT(0x7, 2, 3)
#define X86_FEATURE_MD_CLEAR X86_CPUID_BIT(0x7, 3, 10)
#define X86_FEATURE_IBRS_IBPB X86_CPUID_BIT(0x7, 3, 26)
#define X86_FEATURE_STIBP X86_CPUID_BIT(0x7, 3, 27)
#define X86_FEATURE_L1D_FLUSH X86_CPUID_BIT(0x7, 3, 28)
#define X86_FEATURE_ARCH_CAPABILITIES X86_CPUID_BIT(0x7, 3, 29)
#define X86_FEATURE_SSBD X86_CPUID_BIT(0x7, 3, 31)

#define X86_FEATURE_KVM_PV_CLOCK X86_CPUID_BIT(0x40000001, 0, 3)
#define X86_FEATURE_KVM_PV_EOI X86_CPUID_BIT(0x40000001, 0, 6)
#define X86_FEATURE_KVM_PV_IPI X86_CPUID_BIT(0x40000001, 0, 11)
#define X86_FEATURE_KVM_PV_CLOCK_STABLE X86_CPUID_BIT(0x40000001, 0, 24)

#define X86_FEATURE_AMD_TOPO X86_CPUID_BIT(0x80000001, 2, 22)
#define X86_FEATURE_SYSCALL X86_CPUID_BIT(0x80000001, 3, 11)
#define X86_FEATURE_NX X86_CPUID_BIT(0x80000001, 3, 20)
#define X86_FEATURE_HUGE_PAGE X86_CPUID_BIT(0x80000001, 3, 26)
#define X86_FEATURE_RDTSCP X86_CPUID_BIT(0x80000001, 3, 27)
#define X86_FEATURE_INVAR_TSC X86_CPUID_BIT(0x80000007, 3, 8)

/* legacy accessors */
static inline uint8_t x86_linear_address_width(void) {
  const struct cpuid_leaf* leaf = x86_get_cpuid_leaf(X86_CPUID_ADDR_WIDTH);
  if (!leaf)
    return 0;

  /*
   Extracting bit 15:8 from eax register
   Bits 15-08: #Linear Address Bits
  */
  return (leaf->a >> 8) & 0xff;
}

static inline uint8_t x86_physical_address_width(void) {
  const struct cpuid_leaf* leaf = x86_get_cpuid_leaf(X86_CPUID_ADDR_WIDTH);
  if (!leaf)
    return 0;

  /*
   Extracting bit 7:0 from eax register
   Bits 07-00: #Physical Address Bits
  */
  return leaf->a & 0xff;
}

static inline uint32_t x86_get_clflush_line_size(void) {
  const struct cpuid_leaf* leaf = x86_get_cpuid_leaf(X86_CPUID_MODEL_FEATURES);
  if (!leaf)
    return 0;

  /*
   Extracting bit 15:8 from ebx register
   Bits 15-08: #CLFLUSH line size in quadwords
  */
  return ((leaf->b >> 8) & 0xff) * 8u;
}

/* cpu vendors */
enum x86_vendor_list { X86_VENDOR_UNKNOWN, X86_VENDOR_INTEL, X86_VENDOR_AMD };

extern enum x86_vendor_list x86_vendor;

/* topology */

#define X86_TOPOLOGY_INVALID 0
#define X86_TOPOLOGY_SMT 1
#define X86_TOPOLOGY_CORE 2

struct x86_topology_level {
  /* The number of bits to right shift to identify the next-higher topological
   * level */
  uint8_t right_shift;
  /* The type of relationship this level describes (hyperthread/core/etc) */
  uint8_t type;
};

/**
 * @brief Fetch the topology information for the given level.
 *
 * This interface is uncached.
 *
 * @param level The level to retrieve info for.  Should initially be 0 and
 * incremented with each call.
 * @param info The structure to populate with the discovered information
 *
 * @return true if the requested level existed (and there may be higher levels).
 * @return false if the requested level does not exist (and no higher ones do).
 */
bool x86_topology_enumerate(uint8_t level, struct x86_topology_level* info);

struct x86_model_info {
  uint8_t processor_type;
  uint8_t family;
  uint8_t model;
  uint8_t stepping;

  uint32_t display_family;
  uint32_t display_model;

  uint32_t patch_level;
};

const struct x86_model_info* x86_get_model(void);

enum x86_microarch_list {
  X86_MICROARCH_UNKNOWN,
  X86_MICROARCH_INTEL_NEHALEM,
  X86_MICROARCH_INTEL_WESTMERE,
  X86_MICROARCH_INTEL_SANDY_BRIDGE,
  X86_MICROARCH_INTEL_IVY_BRIDGE,
  X86_MICROARCH_INTEL_BROADWELL,
  X86_MICROARCH_INTEL_HASWELL,
  X86_MICROARCH_INTEL_SKYLAKE,  // Skylake, Kaby Lake, Coffee Lake, Whiskey Lake, Amber Lake...
  X86_MICROARCH_INTEL_CANNONLAKE,
  X86_MICROARCH_INTEL_SILVERMONT,  // Silvermont, Airmont
  X86_MICROARCH_INTEL_GOLDMONT,    // Goldmont
  X86_MICROARCH_INTEL_GOLDMONT_PLUS,
  X86_MICROARCH_AMD_BULLDOZER,
  X86_MICROARCH_AMD_JAGUAR,
  X86_MICROARCH_AMD_ZEN,
};

extern bool g_x86_feature_fsgsbase;
extern bool g_x86_feature_pcid_good;
extern bool g_x86_feature_has_smap;

enum x86_hypervisor_list {
  X86_HYPERVISOR_UNKNOWN,
  X86_HYPERVISOR_NONE,
  X86_HYPERVISOR_KVM,
};

extern enum x86_hypervisor_list x86_hypervisor;

static inline bool x86_hypervisor_has_pv_clock(void) {
  extern bool g_hypervisor_has_pv_clock;
  return g_hypervisor_has_pv_clock;
}

static inline bool x86_hypervisor_has_pv_eoi(void) {
  extern bool g_hypervisor_has_pv_eoi;
  return g_hypervisor_has_pv_eoi;
}

static inline bool x86_hypervisor_has_pv_ipi(void) {
  extern bool g_hypervisor_has_pv_ipi;
  return g_hypervisor_has_pv_ipi;
}

/* returns 0 if unknown, otherwise value in Hz */
typedef uint64_t (*x86_get_timer_freq_func_t)(void);

/* attempt to reboot the system; may fail and simply return */
typedef void (*x86_reboot_system_func_t)(void);

/* attempt to set a reason flag and reboot the system; may fail and simply return */
typedef void (*x86_reboot_reason_func_t)(uint64_t reason);

/* Structure for supporting per-microarchitecture kernel configuration */
typedef struct {
  enum x86_microarch_list x86_microarch;
  x86_get_timer_freq_func_t get_apic_freq;
  x86_get_timer_freq_func_t get_tsc_freq;
  x86_reboot_system_func_t reboot_system;
  x86_reboot_reason_func_t reboot_reason;

  bool disable_c1e;

  // Speculative execution information leak vulnerabilities
  // True iff a microarchitecture is known to have a particular vulnerability. May
  // be overriden by a more specific enumeration mechanism (ex: IA32_ARCH_CAPABILITIES)
  bool has_meltdown;
  bool has_l1tf;
  bool has_mds;
  bool has_swapgs_bug;
  bool has_ssb;

  x86_idle_states_t idle_states;
} x86_microarch_config_t;

static inline const x86_microarch_config_t* x86_get_microarch_config(void) {
  extern const x86_microarch_config_t* x86_microarch_config;
  return x86_microarch_config;
}

static inline bool x86_get_disable_spec_mitigations(void) {
  extern bool g_disable_spec_mitigations;
  return g_disable_spec_mitigations;
}

static inline bool x86_cpu_has_ibpb(void) {
  extern bool g_has_ibpb;
  return g_has_ibpb;
}

static inline bool x86_cpu_should_ras_fill_on_ctxt_switch(void) {
  extern bool g_ras_fill_on_ctxt_switch;
  return g_ras_fill_on_ctxt_switch;
}

static inline bool x86_cpu_vulnerable_to_rsb_underflow(void) {
  extern bool g_cpu_vulnerable_to_rsb_underflow;
  return g_cpu_vulnerable_to_rsb_underflow;
}

static inline bool x86_cpu_should_ibpb_on_ctxt_switch(void) {
  extern bool g_should_ibpb_on_ctxt_switch;
  return g_should_ibpb_on_ctxt_switch;
}

static inline bool x86_cpu_should_mitigate_ssb(void) {
  extern bool g_ssb_mitigated;
  return g_ssb_mitigated;
}

static inline bool x86_cpu_should_l1d_flush_on_vmentry(void) {
  extern bool g_l1d_flush_on_vmentry;
  return g_l1d_flush_on_vmentry;
}

static inline bool x86_cpu_should_md_clear_on_user_return(void) {
  extern bool g_md_clear_on_user_return;
  return g_md_clear_on_user_return;
}

static inline bool x86_cpu_has_enhanced_ibrs(void) {
  extern bool g_has_enhanced_ibrs;
  return g_has_enhanced_ibrs;
}

enum Turbostate {
  ENABLED,
  DISABLED,
};

// Vendor-specific per-cpu init functions, in amd.cpp/intel.cpp
void x86_amd_init_percpu(void);
void x86_intel_init_percpu(void);
#ifdef __cplusplus
bool x86_intel_cpu_has_meltdown(const cpu_id::CpuId* cpuid, MsrAccess* msr);
bool x86_intel_cpu_has_l1tf(const cpu_id::CpuId* cpuid, MsrAccess* msr);
bool x86_intel_cpu_has_mds_taa(const cpu_id::CpuId* cpuid, MsrAccess* msr);
bool x86_intel_cpu_has_swapgs_bug(const cpu_id::CpuId* cpuid);
bool x86_intel_cpu_has_rsb_fallback(const cpu_id::CpuId* cpuid, MsrAccess* msr);
bool x86_intel_cpu_has_ssb(const cpu_id::CpuId* cpuid, MsrAccess* msr);
bool x86_amd_cpu_has_ssb(const cpu_id::CpuId* cpuid, MsrAccess* msr);
bool x86_intel_cpu_has_ssbd(const cpu_id::CpuId* cpuid, MsrAccess* msr);
bool x86_amd_cpu_has_ssbd(const cpu_id::CpuId* cpuid, MsrAccess* msr);
void x86_intel_cpu_set_ssbd(const cpu_id::CpuId* cpuid, MsrAccess* msr);
void x86_amd_cpu_set_ssbd(const cpu_id::CpuId* cpuid, MsrAccess* msr);
void x86_amd_cpu_set_turbo(const cpu_id::CpuId* cpu, MsrAccess* msr, Turbostate state);
void x86_intel_cpu_set_turbo(const cpu_id::CpuId* cpu, MsrAccess* msr, Turbostate state);
void x86_cpu_ibpb(MsrAccess* msr);
bool x86_intel_cpu_has_enhanced_ibrs(const cpu_id::CpuId* cpuid, MsrAccess* msr);
bool x86_amd_cpu_has_ibrs_always_on(const cpu_id::CpuId* cpuid);
void x86_intel_cpu_try_disable_tsx(const cpu_id::CpuId* cpuid, MsrAccess* msr);
// Enable or disable Turbo/CPB on the current CPU
void x86_cpu_set_turbo(const cpu_id::CpuId* cpuid, MsrAccess* msr, Turbostate state);
#endif
uint32_t x86_amd_get_patch_level(void);
uint32_t x86_intel_get_patch_level(void);

__END_CDECLS

#ifdef __cplusplus

const x86_microarch_config_t* get_microarch_config(const cpu_id::CpuId* cpuid);
bool x86_intel_idle_state_may_empty_rsb(X86IdleState*);
bool x86_intel_check_microcode_patch(cpu_id::CpuId* cpuid, MsrAccess* msr, zx_iovec_t patch);
void x86_intel_load_microcode_patch(cpu_id::CpuId* cpuid, MsrAccess* msr, zx_iovec_t patch);

#endif  // __cplusplus

#endif  // ZIRCON_KERNEL_ARCH_X86_INCLUDE_ARCH_X86_FEATURE_H_
