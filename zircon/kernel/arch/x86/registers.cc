// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

/****************************************************************************
 * This file handles detection of supported extended register saving
 * mechanisms.  Of the ones detected, the following is our preference for
 * mechanisms, from best to worst:
 *
 * 1) XSAVES (performs modified+init optimizations, uses compressed register
 *            form, and can save supervisor-only registers)
 * 2) XSAVEOPT (performs modified+init optimizations)
 * 3) XSAVE (no optimizations/compression, but can save all supported extended
 *           registers)
 * 4) FXSAVE (can only save FPU/SSE registers)
 * 5) none (will not save any extended registers, will not allow enabling
 *          features that use extended registers.)
 ****************************************************************************/
#include "arch/x86/registers.h"

#include <inttypes.h>
#include <lib/fit/defer.h>
#include <string.h>
#include <trace.h>
#include <zircon/compiler.h>
#include <zircon/hw/debug/x86.h>

#include <arch/ops.h>
#include <arch/regs.h>
#include <arch/x86.h>
#include <arch/x86/feature.h>
#include <arch/x86/mmu.h>
#include <arch/x86/mp.h>
#include <kernel/auto_lock.h>
#include <kernel/spinlock.h>
#include <kernel/thread.h>
#include <vm/vm.h>

#define LOCAL_TRACE 0

#define IA32_XSS_MSR 0xDA0

// Offset in xsave area that components >= 2 start at.
#define XSAVE_EXTENDED_AREA_OFFSET 576

// The first xsave component in the extended (non-legacy) area.
#define XSAVE_FIRST_EXT_COMPONENT 2

// Number of possible components in the state vector.
#define XSAVE_MAX_COMPONENTS 63

// Bit in XCOMP_BV field of xsave indicating compacted format.
#define XSAVE_XCOMP_BV_COMPACT (1ULL << 63)

static void fxsave(void* register_state);
static void fxrstor(const void* register_state);
static void xrstor(const void* register_state, uint64_t feature_mask);
static void xrstors(const void* register_state, uint64_t feature_mask);
static void xsave(void* register_state, uint64_t feature_mask);
static void xsaveopt(void* register_state, uint64_t feature_mask);
static void xsaves(void* register_state, uint64_t feature_mask);

static void read_xsave_state_info(void);
static void recompute_state_size(void);

// Indexed by component. Components 0 and 1 are the "legacy" floating point and
// SSE ones. These do not have a size or align64 set in this structure since
// they are inside the legacy xsave area. Use XSAVE_FIRST_EXT_COMPONENT for
// the first valid entry.
static struct {
  // Total size of this component in bytes.
  uint32_t size;

  // If true, this component must be aligned to a 64-byte boundary.
  bool align64;
} state_components[XSAVE_MAX_COMPONENTS];

/* Supported bits in XCR0 (each corresponds to a state component) */
static uint64_t xcr0_component_bitmap = 0;
/* Supported bits in IA32_XSS (each corresponds to a state component) */
static uint64_t xss_component_bitmap = 0;
/* Maximum total size for xsave, if all features are enabled */
static size_t xsave_max_area_size = 0;
/* Does this processor support the XSAVES instruction */
static bool xsaves_supported = false;
/* Does this processor support the XSAVEOPT instruction */
static bool xsaveopt_supported = false;
/* Does this processor support the XGETBV instruction with ecx=1 */
static bool xgetbv_1_supported = false;
/* Does this processor support the XSAVE instruction */
static bool xsave_supported = false;
/* Does this processor support FXSAVE */
static bool fxsave_supported = false;
/* Maximum register state size */
static size_t register_state_size = 0;
/* Spinlock to guard register state size changes */
static SpinLock state_lock;

/* For FXRSTOR, we need 512 bytes to save the state.  For XSAVE-based
 * mechanisms, we only need 512 + 64 bytes for the initial state, since
 * our initial state only needs to specify some SSE state (masking exceptions),
 * and XSAVE doesn't require space for any disabled register groups after
 * the last enabled one. */
static uint8_t __ALIGNED(64) extended_register_init_state[512 + 64] = {0};

static_assert(sizeof(x86_xsave_legacy_area) == 416, "Size of legacy xsave area should match spec.");

/* Format described in Intel 3A section 13.4 */
struct xsave_area {
  // Always valid, even when using the older fxsave.
  x86_xsave_legacy_area legacy;

  uint8_t reserved1[96];

  // The xsave header. It and the extended regions are only valid when using xsave, not fxsave.
  uint64_t xstate_bv;
  uint64_t xcomp_bv;
  uint8_t reserved2[48];

  uint8_t extended_region[];
} __PACKED;
static_assert(offsetof(xsave_area, extended_region) == XSAVE_EXTENDED_AREA_OFFSET,
              "xsave_area format should match CPU spec.");

static void x86_extended_register_cpu_init(void) {
  if (likely(xsave_supported)) {
    ulong cr4 = x86_get_cr4();
    /* Enable XSAVE feature set */
    x86_set_cr4(cr4 | X86_CR4_OSXSAVE);
    /* Put xcr0 into a known state (X87 must be enabled in this register) */
    x86_xsetbv(0, X86_XSAVE_STATE_BIT_X87);
  }

  /* Enable the FPU */
  __UNUSED bool enabled = x86_extended_register_enable_feature(X86_EXTENDED_REGISTER_X87);
  DEBUG_ASSERT(enabled);
}

// Sets the portions of the xsave legacy area such that the x87 state is considered in its "initial
// configuration" as defined by Intel Vol 1 section 13.6.
//
// "The x87 state component comprises bytes 23:0 and bytes 159:32." This doesn't count the MXCSR
// register.
static void set_x87_initial_state(x86_xsave_legacy_area* legacy_area) {
  legacy_area->fcw = 0x037f;
  legacy_area->fsw = 0;
  // The initial value of the FTW register is 0xffff. The FTW field in the xsave area is an
  // abbreviated version (see Intel manual sec 13.5.1). In the FTW register 1 bits indicate
  // the empty tag (two per register), while the abbreviated version uses 1 bit per register and
  // 0 indicates empty. So set to 0 to indicate all registers are empty.
  legacy_area->ftw = 0;
  legacy_area->fop = 0;
  legacy_area->fip = 0;
  legacy_area->fdp = 0;

  // Register values are all 0.
  constexpr size_t fp_reg_size = sizeof(legacy_area->st);
  static_assert(fp_reg_size == 128, "Struct size is wrong");
  memset(legacy_area->st, 0, fp_reg_size);
}

// SSE state is only the XMM registers which is all 0 and does not count MXCSR as defined by Intel
// Vol 1 section 13.6.
static void set_sse_initial_state(x86_xsave_legacy_area* legacy_area) {
  constexpr size_t sse_reg_size = sizeof(legacy_area->xmm);
  static_assert(sse_reg_size == 256, "Struct size is wrong");
  memset(legacy_area->xmm, 0, sse_reg_size);
}

/* Figure out what forms of register saving this machine supports and
 * select the best one */
void x86_extended_register_init(void) {
  /* Have we already read the cpu support info */
  static bool info_initialized = false;
  bool initialized_cpu_already = false;

  if (!info_initialized) {
    DEBUG_ASSERT(arch_curr_cpu_num() == 0);

    read_xsave_state_info();
    info_initialized = true;

    /* We currently assume that if xsave isn't support fxsave is */
    fxsave_supported = x86_feature_test(X86_FEATURE_FXSR);

    /* Set up initial states */
    if (likely(fxsave_supported || xsave_supported)) {
      x86_extended_register_cpu_init();
      initialized_cpu_already = true;

      // Intel Vol 3 section 13.5.4 describes the XSAVE initialization. The only change we
      // want to make to the init state is having SIMD exceptions masked. The "legacy" area
      // of the xsave structure is valid for fxsave as well.
      xsave_area* area = reinterpret_cast<xsave_area*>(extended_register_init_state);
      set_x87_initial_state(&area->legacy);
      set_sse_initial_state(&area->legacy);
      area->legacy.mxcsr = 0x3f << 7;

      if (xsave_supported) {
        area->xstate_bv |= X86_XSAVE_STATE_BIT_SSE;

        /* If xsaves is being used, then make the saved state be in
         * compact form.  xrstors will GPF if it is not. */
        if (xsaves_supported) {
          area->xcomp_bv |= XSAVE_XCOMP_BV_COMPACT;
          area->xcomp_bv |= area->xstate_bv;
        }
      }
    }

    if (likely(xsave_supported)) {
      recompute_state_size();
    } else if (fxsave_supported) {
      register_state_size = 512;
    }
  }
  /* Ensure that xsaves_supported == true implies xsave_supported == true */
  DEBUG_ASSERT(!xsaves_supported || xsave_supported);
  /* Ensure that xsaveopt_supported == true implies xsave_supported == true */
  DEBUG_ASSERT(!xsaveopt_supported || xsave_supported);

  if (!initialized_cpu_already) {
    x86_extended_register_cpu_init();
  }
}

bool x86_extended_register_enable_feature(enum x86_extended_register_feature feature) {
  /* We currently assume this is only called during initialization.
   * We rely on interrupts being disabled so xgetbv/xsetbv will not be
   * racey */
  DEBUG_ASSERT(arch_ints_disabled());

  switch (feature) {
    case X86_EXTENDED_REGISTER_X87: {
      if (unlikely(!x86_feature_test(X86_FEATURE_FPU) || (!fxsave_supported && !xsave_supported))) {
        return false;
      }

      /* No x87 emul, monitor co-processor */
      ulong cr0 = x86_get_cr0();
      cr0 &= ~X86_CR0_EM;
      cr0 |= X86_CR0_NE;
      cr0 |= X86_CR0_MP;
      x86_set_cr0(cr0);

      /* Init x87, starts with exceptions masked */
      __asm__ __volatile__("finit" : : : "memory");

      if (likely(xsave_supported)) {
        x86_xsetbv(0, x86_xgetbv(0) | X86_XSAVE_STATE_BIT_X87);
      }
      break;
    }
    case X86_EXTENDED_REGISTER_SSE: {
      if (unlikely(!x86_feature_test(X86_FEATURE_SSE) || !x86_feature_test(X86_FEATURE_FXSR))) {
        return false;
      }

      /* Init SSE */
      ulong cr4 = x86_get_cr4();
      cr4 |= X86_CR4_OSXMMEXPT;
      cr4 |= X86_CR4_OSFXSR;
      x86_set_cr4(cr4);

      /* mask all exceptions */
      uint32_t mxcsr = 0;
      __asm__ __volatile__("stmxcsr %0" : "=m"(mxcsr));
      mxcsr = (0x3f << 7);
      __asm__ __volatile__("ldmxcsr %0" : : "m"(mxcsr));

      if (likely(xsave_supported)) {
        x86_xsetbv(0, x86_xgetbv(0) | X86_XSAVE_STATE_BIT_SSE);
      }
      break;
    }
    case X86_EXTENDED_REGISTER_AVX: {
      if (!xsave_supported || !(xcr0_component_bitmap & X86_XSAVE_STATE_BIT_AVX)) {
        return false;
      }

      /* Enable SIMD exceptions */
      ulong cr4 = x86_get_cr4();
      cr4 |= X86_CR4_OSXMMEXPT;
      x86_set_cr4(cr4);

      x86_xsetbv(0, x86_xgetbv(0) | X86_XSAVE_STATE_BIT_AVX);
      break;
    }
    case X86_EXTENDED_REGISTER_MPX: {
      /* Currently unsupported */
      return false;
    }
    case X86_EXTENDED_REGISTER_AVX512: {
      /* Currently unsupported */
      return false;
    }
    case X86_EXTENDED_REGISTER_PT: {
      /* Currently unsupported */
      return false;
    }
    case X86_EXTENDED_REGISTER_PKRU: {
      /* Currently unsupported */
      return false;
    }
    default:
      return false;
  }

  recompute_state_size();
  return true;
}

size_t x86_extended_register_size(void) { return register_state_size; }

size_t x86_extended_register_max_size(void) { return xsave_max_area_size; }

uint64_t x86_extended_xcr0_component_bitmap(void) { return xcr0_component_bitmap; }

bool x86_xsave_supported(void) { return xsave_supported; }

void x86_extended_register_init_state(void* register_state) {
  // Copy the initialization state; this overcopies on systems that fall back
  // to fxsave, but the buffer is required to be large enough.
  memcpy(register_state, extended_register_init_state, sizeof(extended_register_init_state));
}

void x86_extended_register_init_state_from_bv(void* register_state, uint64_t xstate_bv) {
  x86_extended_register_init_state(register_state);

  if (!xsave_supported) {
    return;
  }
  xsave_area* area = reinterpret_cast<xsave_area*>(register_state);
  area->xstate_bv = xstate_bv;

  // Ensure that xcomp_bv matches xstate_bv and that the saved state is in compact form. If any
  // bit in xcomp_bv is 0 and the corresponding bit in xstate_bv is 1, or if
  // XSAVE_XCOMP_BV_COMPACT is not set, xrstors will GPF.
  if (xsaves_supported) {
    area->xcomp_bv = area->xstate_bv;
    area->xcomp_bv |= XSAVE_XCOMP_BV_COMPACT;
  }
}

void x86_extended_register_save_state(void* register_state) {
  /* The idle threads have no extended register state */
  if (unlikely(!register_state)) {
    return;
  }

  if (xsaves_supported) {
    xsaves(register_state, ~0ULL);
  } else if (xsaveopt_supported) {
    xsaveopt(register_state, ~0ULL);
  } else if (xsave_supported) {
    xsave(register_state, ~0ULL);
  } else if (fxsave_supported) {
    fxsave(register_state);
  }
}

void x86_extended_register_restore_state(const void* register_state) {
  /* The idle threads have no extended register state */
  if (unlikely(!register_state)) {
    return;
  }

  if (xsaves_supported) {
    xrstors(register_state, ~0ULL);
  } else if (xsave_supported) {
    xrstor(register_state, ~0ULL);
  } else if (fxsave_supported) {
    fxrstor(register_state);
  }
}

void x86_extended_register_context_switch(Thread* old_thread, const Thread* new_thread) {
  if (likely(old_thread)) {
    x86_extended_register_save_state(old_thread->arch().extended_register_buffer);
  }
  x86_extended_register_restore_state(new_thread->arch().extended_register_buffer);
}

static void read_xsave_state_info(void) {
  xsave_supported = x86_feature_test(X86_FEATURE_XSAVE);
  if (!xsave_supported) {
    LTRACEF("xsave not supported\n");
    return;
  }

  /* if we bail, set everything to unsupported */
  auto unsupported_fallback = fit::defer([]() {
    xsave_supported = false;
    xsaves_supported = false;
    xsaveopt_supported = false;
  });

  /* This procedure is described in Intel Vol 1 section 13.2 */

  /* Read feature support from subleaves 0 and 1 */
  struct cpuid_leaf leaf;
  if (!x86_get_cpuid_subleaf(X86_CPUID_XSAVE, 0, &leaf)) {
    LTRACEF("could not find xsave leaf\n");
    return;
  }
  xcr0_component_bitmap = ((uint64_t)leaf.d << 32) | leaf.a;
  size_t max_area = XSAVE_EXTENDED_AREA_OFFSET;

  x86_get_cpuid_subleaf(X86_CPUID_XSAVE, 1, &leaf);
  xgetbv_1_supported = !!(leaf.a & (1 << 2));
  xsaves_supported = !!(leaf.a & (1 << 3));
  xsaveopt_supported = !!(leaf.a & (1 << 0));
  xss_component_bitmap = ((uint64_t)leaf.d << 32) | leaf.c;

  LTRACEF("xcr0 bitmap: %016" PRIx64 "\n", xcr0_component_bitmap);
  LTRACEF("xss bitmap: %016" PRIx64 "\n", xss_component_bitmap);

  /* Sanity check; all CPUs that support xsave support components 0 and 1 */
  DEBUG_ASSERT((xcr0_component_bitmap & 0x3) == 0x3);
  if ((xcr0_component_bitmap & 0x3) != 0x3) {
    LTRACEF("unexpected xcr0 bitmap %016" PRIx64 "\n", xcr0_component_bitmap);
    return;
  }

  /* we're okay from now on out */
  unsupported_fallback.cancel();

  /* Read info about the state components */
  for (int i = XSAVE_FIRST_EXT_COMPONENT; i < XSAVE_MAX_COMPONENTS; ++i) {
    if (!(xcr0_component_bitmap & (1ULL << i)) && !(xss_component_bitmap & (1ULL << i))) {
      continue;
    }
    x86_get_cpuid_subleaf(X86_CPUID_XSAVE, i, &leaf);

    bool align64 = !!(leaf.c & 0x2);

    state_components[i].size = leaf.a;
    state_components[i].align64 = align64;
    LTRACEF("component %d size: %u (xcr0 %d)\n", i, state_components[i].size,
            !!(xcr0_component_bitmap & (1ULL << i)));

    if (align64) {
      max_area = ROUNDUP(max_area, 64);
    }
    max_area += leaf.a;
  }
  xsave_max_area_size = max_area;
  LTRACEF("total xsave size: %zu\n", max_area);

  return;
}

static void recompute_state_size(void) {
  if (!xsave_supported) {
    return;
  }

  size_t new_size = 0;
  /* If we're in a compacted form, compute the total size.  The algorithm
   * for this is defined in Intel Vol 1 section 13.4.3 */
  if (xsaves_supported) {
    new_size = XSAVE_EXTENDED_AREA_OFFSET;
    uint64_t enabled_features = x86_xgetbv(0) | read_msr(IA32_XSS_MSR);
    for (int i = XSAVE_FIRST_EXT_COMPONENT; i < XSAVE_MAX_COMPONENTS; ++i) {
      if (!(enabled_features & (1ULL << i))) {
        continue;
      }

      if (state_components[i].align64) {
        new_size = ROUNDUP(new_size, 64);
      }
      new_size += state_components[i].size;
    }
  } else {
    /* Otherwise, use CPUID.(EAX=0xD,ECX=1):EBX, which stores the computed
     * maximum size required for saving everything specified in XCR0 */
    struct cpuid_leaf leaf;
    x86_get_cpuid_subleaf(X86_CPUID_XSAVE, 0, &leaf);
    new_size = leaf.b;
  }

  AutoSpinLockNoIrqSave guard(&state_lock);
  /* Only allow size to increase; all CPUs should converge to the same value,
   * but for sanity let's keep it monotonically increasing */
  if (new_size > register_state_size) {
    register_state_size = new_size;
    DEBUG_ASSERT(register_state_size <= X86_MAX_EXTENDED_REGISTER_SIZE);
  }
}

static void fxsave(void* register_state) {
  __asm__ __volatile__("fxsave %0" : "=m"(*(uint8_t*)register_state) : : "memory");
}

static void fxrstor(const void* register_state) {
  __asm__ __volatile__("fxrstor %0" : : "m"(*(uint8_t*)register_state) : "memory");
}

static void xrstor(const void* register_state, uint64_t feature_mask) {
  __asm__ volatile("xrstor %0"
                   :
                   : "m"(*(uint8_t*)register_state), "d"((uint32_t)(feature_mask >> 32)),
                     "a"((uint32_t)feature_mask)
                   : "memory");
}

static void xrstors(const void* register_state, uint64_t feature_mask) {
  __asm__ volatile("xrstors %0"
                   :
                   : "m"(*(uint8_t*)register_state), "d"((uint32_t)(feature_mask >> 32)),
                     "a"((uint32_t)feature_mask)
                   : "memory");
}

static void xsave(void* register_state, uint64_t feature_mask) {
  __asm__ volatile("xsave %0"
                   : "+m"(*(uint8_t*)register_state)
                   : "d"((uint32_t)(feature_mask >> 32)), "a"((uint32_t)feature_mask)
                   : "memory");
}

static void xsaveopt(void* register_state, uint64_t feature_mask) {
  __asm__ volatile("xsaveopt %0"
                   : "+m"(*(uint8_t*)register_state)
                   : "d"((uint32_t)(feature_mask >> 32)), "a"((uint32_t)feature_mask)
                   : "memory");
}

static void xsaves(void* register_state, uint64_t feature_mask) {
  __asm__ volatile("xsaves %0"
                   : "+m"(*(uint8_t*)register_state)
                   : "d"((uint32_t)(feature_mask >> 32)), "a"((uint32_t)feature_mask)
                   : "memory");
}

uint64_t x86_xgetbv(uint32_t reg) {
  uint32_t hi, lo;
  __asm__ volatile("xgetbv" : "=d"(hi), "=a"(lo) : "c"(reg) : "memory");
  return ((uint64_t)hi << 32) + lo;
}

void x86_xsetbv(uint32_t reg, uint64_t val) {
  __asm__ volatile("xsetbv"
                   :
                   : "c"(reg), "d"((uint32_t)(val >> 32)), "a"((uint32_t)val)
                   : "memory");
}

void* x86_get_extended_register_state_component(void* register_state, uint32_t component,
                                                bool mark_present, uint32_t* size) {
  if (component >= XSAVE_MAX_COMPONENTS) {
    *size = 0;
    return nullptr;
  }

  xsave_area* area = reinterpret_cast<xsave_area*>(register_state);

  uint64_t state_component_bit = (1ul << component);

  // Components 0 and 1 are special and are always present in the legacy area.
  if (component <= 1) {
    *size = sizeof(x86_xsave_legacy_area);
    if (!(area->xstate_bv & state_component_bit)) {
      // Component not written because registers were in the initial configuration. Set it so
      // the caller sees the correct initial values.
      if (component == 0) {
        set_x87_initial_state(&area->legacy);
      } else {
        set_sse_initial_state(&area->legacy);
      }
      if (mark_present) {
        area->xstate_bv |= state_component_bit;
      }
    }

    return area;
  }

  if (!(area->xcomp_bv & XSAVE_XCOMP_BV_COMPACT)) {
    // Standard format. The offset and size are provided by a static CPUID call.
    cpuid_leaf leaf;
    x86_get_cpuid_subleaf(X86_CPUID_XSAVE, component, &leaf);
    *size = leaf.a;
    if (leaf.a == 0) {
      return nullptr;
    }
    uint8_t* component_begin = static_cast<uint8_t*>(register_state) + leaf.b;

    if (!(area->xstate_bv & state_component_bit)) {
      // Component not written because it's in the initial state. Write the initial values to
      // the structure the caller sees the correct data. The initial state of all non-x87
      // xsave components (x87 is handled above) is all 0's.
      memset(component_begin, 0, *size);
      if (mark_present) {
        area->xstate_bv |= state_component_bit;
      }
    }
    return component_begin;
  }

  // Compacted format used. The corresponding bit in xcomp_bv indicates whether the component is
  // present.
  if (!(area->xcomp_bv & state_component_bit)) {
    // Currently this doesn't support reading or writing compacted components that aren't
    // currently marked present. In the future, we may want to add this which will require
    // rewriting all the following components.
    *size = 0;
    return nullptr;
  }

  // Walk all present components and add up their sizes (optionally aligned up) to get the offset.
  uint32_t offset = XSAVE_EXTENDED_AREA_OFFSET;
  for (uint32_t i = XSAVE_FIRST_EXT_COMPONENT; i < component; i++) {
    if (!(area->xcomp_bv & (1ul << i))) {
      continue;
    }
    if (state_components[i].align64) {
      offset = ROUNDUP(offset, 64);
    }
    offset += state_components[i].size;
  }
  if (state_components[component].align64) {
    offset = ROUNDUP(offset, 64);
  }

  uint8_t* component_begin = static_cast<uint8_t*>(register_state) + offset;
  *size = state_components[component].size;

  if (!(area->xstate_bv & state_component_bit)) {
    // Component not written because it's in the initial state. Write the initial values to
    // the structure the caller sees the correct data. The initial state of all non-x87
    // xsave components (x87 is handled above) is all 0's.
    memset(component_begin, 0, *size);
    if (mark_present) {
      area->xstate_bv |= state_component_bit;
    }
  }
  return component_begin;
}

// Debug Registers --------------------------------------------------------------------------------

/* Validates whether this is a valid address to save into a debug register.
 * Will mask out reserved bits to their expected values. */
bool x86_validate_debug_state(x86_debug_state_t* debug_state) {
  // Validate the addresses being written.
  for (size_t i = 0; i < 4; i++) {
    uint64_t addr = debug_state->dr[i];
    if (addr == 0)
      continue;

    if (!is_user_accessible(addr) || !x86_is_vaddr_canonical(addr)) {
      return false;
    }
  }

  // DR6 is not writable from userspace, as it is a debug status registers.
  // Zircon takes on the job of keeping it up to date.

  // DR7.
  uint64_t dr7 = debug_state->dr7;
  debug_state->dr7 = X86_DR7_MASK;
  uint64_t values_to_write2 = dr7 & X86_DR7_USER_MASK;
  debug_state->dr7 |= values_to_write2;

  return true;
}

void x86_read_debug_status(uint64_t* dr6) {
  // NOTE: There is a difference in bit 16 between Intel64 and AMD64.
  //       In AMD, bit 16 is reserved and always set to 0.
  //       In Intel, it can mean that an debug or breakpoint exception occurred during a RTM
  //       block. For now, we mask this bit to make both platforms uniform.
  uint64_t value = 0;
  asm volatile("mov %%dr6, %0" : "=r"(value));
  *dr6 = value | X86_DR6_MASK;
}

void x86_write_debug_status(uint64_t dr6) {
  // Upper 32 bits must be 0.
  uint64_t full_value = dr6 & 0x00000000ffffffff;
  asm volatile("mov %0, %%dr6" ::"r"(full_value));
}

void x86_disable_debug_state(void) {
  // Disabling dr7 is enough to disable the debug functionality.
  uint64_t zero_val = 0;
  asm volatile("mov %0, %%dr7" ::"r"(zero_val));
}

void x86_read_hw_debug_regs(x86_debug_state_t* debug_state) {
  asm volatile("mov %%dr0, %0" : "=r"(debug_state->dr[0]));
  asm volatile("mov %%dr1, %0" : "=r"(debug_state->dr[1]));
  asm volatile("mov %%dr2, %0" : "=r"(debug_state->dr[2]));
  asm volatile("mov %%dr3, %0" : "=r"(debug_state->dr[3]));

  x86_read_debug_status(&debug_state->dr6);

  asm volatile("mov %%dr7, %0" : "=r"(debug_state->dr7));
}

void x86_write_hw_debug_regs(const x86_debug_state_t* debug_state) {
  asm volatile("mov %0, %%dr0" ::"r"(debug_state->dr[0]));
  asm volatile("mov %0, %%dr1" ::"r"(debug_state->dr[1]));
  asm volatile("mov %0, %%dr2" ::"r"(debug_state->dr[2]));
  asm volatile("mov %0, %%dr3" ::"r"(debug_state->dr[3]));
  // DR6 is not writable from userspace.
  // IMPORTANT: DR7 should be already masked at this point by calling x86_validate_debug_state.
  asm volatile("mov %0, %%dr7" ::"r"(debug_state->dr7));
}

#ifndef NDEBUG

void x86_print_dr6(uint64_t dr6) {
  printf("0x%08lx: B0=%d, B1=%d, B2=%d, B3=%d, BD=%d, BS=%d, BT=%d\n", dr6,
         (int)X86_DBG_STATUS_B0_GET(dr6), (int)X86_DBG_STATUS_B1_GET(dr6),
         (int)X86_DBG_STATUS_B2_GET(dr6), (int)X86_DBG_STATUS_B3_GET(dr6),
         (int)X86_DBG_STATUS_BD_GET(dr6), (int)X86_DBG_STATUS_BS_GET(dr6),
         (int)X86_DBG_STATUS_BT_GET(dr6));
}

void x86_print_dr7(uint64_t dr7) {
  printf(
      "0x%08lx: L0=%d, G0=%d, L1=%d, G1=%d, L2=%d, G2=%d, L3=%d, G4=%d, LE=%d, GE=%d, GD=%d\n"
      "%*sR/W0=%d, LEN0=%d, R/W1=%d, LEN1=%d, R/W2=%d, LEN2=%d, R/W3=%d, LEN3=%d\n",
      dr7, (int)X86_DBG_CONTROL_L0_GET(dr7), (int)X86_DBG_CONTROL_G0_GET(dr7),
      (int)X86_DBG_CONTROL_L1_GET(dr7), (int)X86_DBG_CONTROL_G1_GET(dr7),
      (int)X86_DBG_CONTROL_L2_GET(dr7), (int)X86_DBG_CONTROL_G2_GET(dr7),
      (int)X86_DBG_CONTROL_L3_GET(dr7), (int)X86_DBG_CONTROL_G3_GET(dr7),
      (int)X86_DBG_CONTROL_LE_GET(dr7), (int)X86_DBG_CONTROL_GE_GET(dr7),
      (int)X86_DBG_CONTROL_GD_GET(dr7), 12, " ", (int)X86_DBG_CONTROL_RW0_GET(dr7),
      (int)X86_DBG_CONTROL_LEN0_GET(dr7), (int)X86_DBG_CONTROL_RW1_GET(dr7),
      (int)X86_DBG_CONTROL_LEN1_GET(dr7), (int)X86_DBG_CONTROL_RW2_GET(dr7),
      (int)X86_DBG_CONTROL_LEN2_GET(dr7), (int)X86_DBG_CONTROL_RW3_GET(dr7),
      (int)X86_DBG_CONTROL_LEN3_GET(dr7));
}

#endif

void PrintFrame(FILE* file, const iframe_t& frame) {
  fprintf(file, " CS:  %#18" PRIx64 " RIP: %#18" PRIx64 " EFL: %#18" PRIx64 " CR2: %#18lx\n",
          frame.cs, frame.ip, frame.flags, x86_get_cr2());
  fprintf(file,
          " RAX: %#18" PRIx64 " RBX: %#18" PRIx64 " RCX: %#18" PRIx64 " RDX: %#18" PRIx64 "\n",
          frame.rax, frame.rbx, frame.rcx, frame.rdx);
  fprintf(file,
          " RSI: %#18" PRIx64 " RDI: %#18" PRIx64 " RBP: %#18" PRIx64 " RSP: %#18" PRIx64 "\n",
          frame.rsi, frame.rdi, frame.rbp, frame.user_sp);
  fprintf(file,
          "  R8: %#18" PRIx64 "  R9: %#18" PRIx64 " R10: %#18" PRIx64 " R11: %#18" PRIx64 "\n",
          frame.r8, frame.r9, frame.r10, frame.r11);
  fprintf(file,
          " R12: %#18" PRIx64 " R13: %#18" PRIx64 " R14: %#18" PRIx64 " R15: %#18" PRIx64 "\n",
          frame.r12, frame.r13, frame.r14, frame.r15);
  fprintf(file, "errc: %#18" PRIx64 "\n", frame.err_code);
}
