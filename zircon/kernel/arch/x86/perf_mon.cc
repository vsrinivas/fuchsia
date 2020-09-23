// Copyright 2017 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

// This file contains the lower part of Intel Performance Monitor support that
// must be done in the kernel (so that we can read/write msrs).
// The common code is in kernel/lib/perfmon/perfmon.cpp.
// The userspace driver is in system/dev/misc/cpu-trace/intel-pm.c.

// TODO(dje): See Intel Vol 3 18.2.3.1 for hypervisor recommendations.
// TODO(dje): LBR, BTS, et.al. See Intel Vol 3 Chapter 17.
// TODO(dje): PMI mitigations
// TODO(dje): Eventually may wish to virtualize some/all of the MSRs,
//            some have multiple disparate uses.
// TODO(dje): vmo management
// TODO(dje): check hyperthread handling
// TODO(dje): See about reducing two loops (programmable+fixed) into one.
// TODO(dje): If we're using one counter as the trigger, we could skip
// resetting the other counters and instead record the last value (so that we
// can continue to emit the delta into the trace buffer) - assuming the write
// to memory is faster than the wrmsr which is apparently true.
// TODO(dje): rdpmc
#include "arch/x86/perf_mon.h"

#include <assert.h>
#include <err.h>
#include <lib/ktrace.h>
#include <lib/pci/pio.h>
#include <lib/perfmon.h>
#include <lib/zircon-internal/mtrace.h>
#include <lib/zircon-internal/thread_annotations.h>
#include <platform.h>
#include <pow2.h>
#include <string.h>
#include <trace.h>

#include <new>

#include <arch/arch_ops.h>
#include <arch/mmu.h>
#include <arch/x86.h>
#include <arch/x86/apic.h>
#include <arch/x86/feature.h>
#include <arch/x86/mmu.h>
#include <fbl/algorithm.h>
#include <fbl/alloc_checker.h>
#include <fbl/macros.h>
#include <fbl/ref_ptr.h>
#include <kernel/align.h>
#include <kernel/cpu.h>
#include <kernel/mp.h>
#include <kernel/mutex.h>
#include <kernel/stats.h>
#include <kernel/thread.h>
#include <ktl/iterator.h>
#include <ktl/move.h>
#include <ktl/unique_ptr.h>
#include <lk/init.h>
#include <vm/vm.h>
#include <vm/vm_address_region.h>
#include <vm/vm_aspace.h>
#include <vm/vm_object_physical.h>

#define LOCAL_TRACE 0

static void x86_perfmon_reset_task(void* raw_context);

// TODO(cja): Sort out headers so the kernel can include these sorts of definitions
// without needing DDK access
#define PCI_CONFIG_VENDOR_ID 0x00
#define PCI_CONFIG_DEVICE_ID 0x02

// There's only a few misc events, and they're non-homogeneous,
// so handle them directly.
typedef enum {
#define DEF_MISC_SKL_EVENT(symbol, event_name, id, offset, size, flags, readable_name, \
                           description)                                                \
  symbol##_ID = perfmon::MakeEventId(perfmon::kGroupMisc, id),
#include <lib/zircon-internal/device/cpu-trace/skylake-misc-events.inc>
} misc_event_id_t;

// h/w address of misc events.
typedef enum {
#define DEF_MISC_SKL_EVENT(symbol, event_name, id, offset, size, flags, readable_name, \
                           description)                                                \
  symbol##_OFFSET = offset,
#include <lib/zircon-internal/device/cpu-trace/skylake-misc-events.inc>
} misc_event_offset_t;

// TODO(dje): Freeze-on-PMI doesn't work in skylake.
// This is here for experimentation purposes.
#define TRY_FREEZE_ON_PMI 0

// At a minimum we require Performance Monitoring version 4.
// KISS: Skylake supports version 4.
#define MINIMUM_INTEL_PERFMON_VERSION 4

// MSRs

#define IA32_PLATFORM_INFO 0xce

#define IA32_PERF_CAPABILITIES 0x345

// The counter MSR addresses are contiguous from here.
#define IA32_PMC_FIRST 0x0c1
// The event selection MSR addresses are contiguous from here.
#define IA32_PERFEVTSEL_FIRST 0x186

#define IA32_FIXED_CTR_CTRL 0x38d

// The fixed counter MSR addresses are contiguous from here.
#define IA32_FIXED_CTR0 0x309

#define IA32_PERF_GLOBAL_CTRL 0x38f
#define IA32_PERF_GLOBAL_STATUS 0x38e
#define IA32_PERF_GLOBAL_OVF_CTRL 0x390
#define IA32_PERF_GLOBAL_STATUS_RESET 0x390  // Yes, same as OVF_CTRL.
#define IA32_PERF_GLOBAL_STATUS_SET 0x391
#define IA32_PERF_GLOBAL_INUSE 0x392

#define IA32_DEBUGCTL 0x1d9

#define SKL_LAST_BRANCH_SELECT 0x1c8
#define SKL_LAST_BRANCH_TOS 0x1c9

// N.B. These values have changed across models.
#define SKL_LAST_BRANCH_FROM_0 0x680
#define SKL_LAST_BRANCH_FROM_16 0x690
#define SKL_LAST_BRANCH_TO_0 0x6c0
#define SKL_LAST_BRANCH_TO_16 0x6d0
#define SKL_LAST_BRANCH_INFO_0 0xdc0
#define SKL_LAST_BRANCH_INFO_16 0xdd0

// Vendor,device ids of the device with MCHBAR stats registers.
#define INTEL_MCHBAR_PCI_VENDOR_ID 0x8086
const uint16_t supported_mem_device_ids[] = {
    0x1900,  // docs use this value
    0x1904,  // seen on NUC6
    0x5904,  // seen on NUC7
};

// Offset in PCI config space of the BAR (base address register) of the
// MCHBAR stats registers.
#define INTEL_MCHBAR_PCI_CONFIG_OFFSET 0x48

// Offsets from the BAR in the memory controller hub mmio space of counters
// we're interested in. See the specs for MCHBAR in, e.g.,
// "6th Generation Intel Core Processor Family Datasheet, Vol. 2".
// TODO(dje): These values are model specific. The current values work for
// currently supported platforms. Need to detect when we're on a supported
// platform.
// The BEGIN/END values are for computing the page(s) we need to map.
// Offset from BAR of the first byte we need to map.
#define UNC_IMC_STATS_BEGIN 0x5040  // MISC_MEM_GT_REQUESTS
// Offset from BAR of the last byte we need to map.
#define UNC_IMC_STATS_END 0x5983  // MISC_PKG_GT_TEMP

// Verify all values are within [BEGIN,END].
#define DEF_MISC_SKL_EVENT(symbol, event_name, id, offset, size, flags, readable_name, \
                           description)                                                \
  &&(offset >= UNC_IMC_STATS_BEGIN && (offset + size / 8) <= UNC_IMC_STATS_END + 1)
static_assert(1
#include <lib/zircon-internal/device/cpu-trace/skylake-misc-events.inc>
              ,
              "");

// These aren't constexpr as we iterate to fill in values for each counter.
static uint64_t kGlobalCtrlWritableBits;
static uint64_t kFixedCounterCtrlWritableBits;

// Commented out values represent currently unsupported features.
// They remain present for documentation purposes.
// Note: Making this const assumes at least PM version >= 2 (e.g.,
// IA32_DEBUGCTL_FREEZE_LBRS_ON_PMI_MASK).
// Note: At least FREEZE_WHILE_SMM needs to be set based on a runtime
// determination (need to check PERF_CAPABILITIES).
static constexpr uint64_t kDebugCtrlWritableBits = (IA32_DEBUGCTL_LBR_MASK |
                                                    /*IA32_DEBUGCTL_BTF_MASK |*/
                                                    /*IA32_DEBUGCTL_TR_MASK |*/
                                                    /*IA32_DEBUGCTL_BTS_MASK |*/
                                                    /*IA32_DEBUGCTL_BTINT_MASK |*/
                                                    /*IA32_DEBUGCTL_BTS_OFF_OS_MASK |*/
                                                    /*IA32_DEBUGCTL_BTS_OFF_USR_MASK |*/
                                                    IA32_DEBUGCTL_FREEZE_LBRS_ON_PMI_MASK |
#if TRY_FREEZE_ON_PMI
                                                    IA32_DEBUGCTL_FREEZE_PERFMON_ON_PMI_MASK |
#endif
                                                    /*IA32_DEBUGCTL_FREEZE_WHILE_SMM_MASK |*/
                                                    /*IA32_DEBUGCTL_RTM_MASK |*/
                                                    0);
static constexpr uint64_t kEventSelectWritableBits =
    (IA32_PERFEVTSEL_EVENT_SELECT_MASK | IA32_PERFEVTSEL_UMASK_MASK | IA32_PERFEVTSEL_USR_MASK |
     IA32_PERFEVTSEL_OS_MASK | IA32_PERFEVTSEL_E_MASK | IA32_PERFEVTSEL_PC_MASK |
     IA32_PERFEVTSEL_INT_MASK | IA32_PERFEVTSEL_ANY_MASK | IA32_PERFEVTSEL_EN_MASK |
     IA32_PERFEVTSEL_INV_MASK | IA32_PERFEVTSEL_CMASK_MASK);

enum LbrFormat {
  LBR_FORMAT_32 = 0,
  // The format contains LBR_INFO in addition to LBR_FROM/LBR_TO.
  LBR_FORMAT_INFO = 0b101,
};

static bool perfmon_hw_initialized = false;

static uint16_t perfmon_version = 0;

// The maximum number of programmable counters that can be simultaneously
// handled, and their maximum width;
static uint16_t perfmon_num_programmable_counters = 0;
static uint16_t perfmon_programmable_counter_width = 0;

// The maximum number of fixed counters that can be simultaneously
// handled, and their maximum width;
static uint16_t perfmon_num_fixed_counters = 0;
static uint16_t perfmon_fixed_counter_width = 0;

static uint32_t perfmon_unsupported_events = 0;
static uint32_t perfmon_capabilities = 0;

// Maximum counter values, derived from their width.
static uint64_t perfmon_max_fixed_counter_value = 0;
static uint64_t perfmon_max_programmable_counter_value = 0;

// Number of entries we can write in an LBR record.
static uint32_t perfmon_lbr_stack_size = 0;

// Counter bits in GLOBAL_STATUS to check on each interrupt.
static uint64_t perfmon_counter_status_bits = 0;

// BAR (base address register) of Intel MCHBAR performance
// registers. These registers are accessible via mmio.
static uint32_t perfmon_mchbar_bar = 0;

// The maximum number of "miscellaneous" events we can handle at once
// and their width. This is mostly for information purposes, there may be
// additional constraints which depend on the counters in question.
static uint16_t perfmon_num_misc_events = 0;
static uint16_t perfmon_misc_counter_width = 64;

struct MemoryControllerHubData {
  // Where the regs are mapped.
  fbl::RefPtr<VmMapping> mapping;

  // The address where UNC_IMC_STATS_BEGIN is mapped, or zero if not mapped.
  volatile void* stats_addr = 0;

  // We can't reset the events, and even if we could it's preferable to
  // avoid making the device writable (lots of critical stuff in there),
  // so record the previous values so that we can emit into the trace buffer
  // the delta since the last interrupt.
  struct {
    uint32_t bytes_read = 0;
    uint32_t bytes_written = 0;
    uint32_t gt_requests = 0;
    uint32_t ia_requests = 0;
    uint32_t io_requests = 0;
    uint64_t all_active_core_cycles = 0;
    uint64_t any_active_core_cycles = 0;
    uint64_t active_gt_cycles = 0;
    uint64_t active_ia_gt_cycles = 0;
    uint64_t active_gt_slice_cycles = 0;
    uint64_t active_gt_engine_cycles = 0;
    // The remaining registers don't count anything.
  } last_mem;
};

struct PerfmonState : public PerfmonStateBase {
  static zx_status_t Create(unsigned n_cpus, ktl::unique_ptr<PerfmonState>* out_state);
  explicit PerfmonState(unsigned n_cpus);

  // IA32_PERF_GLOBAL_CTRL
  uint64_t global_ctrl = 0;

  // IA32_FIXED_CTR_CTRL
  uint64_t fixed_ctrl = 0;

  // IA32_DEBUGCTL
  uint64_t debug_ctrl = 0;

  // True if MCHBAR perf regs need to be mapped in.
  bool need_mchbar = false;

  // See intel-pm.h:X86PmuConfig.
  PmuEventId timebase_event = perfmon::kEventIdNone;

  // The number of each kind of event in use, so we don't have to iterate
  // over the entire arrays.
  unsigned num_used_fixed = 0;
  unsigned num_used_programmable = 0;
  unsigned num_used_misc = 0;

  // True if last branch records have been requested.
  bool request_lbr_record = false;

  MemoryControllerHubData mchbar_data;

  // |fixed_hw_map[i]| is the h/w fixed counter number.
  // This is used to only look at fixed counters that are used.
  unsigned fixed_hw_map[IPM_MAX_FIXED_COUNTERS] = {};

  // The ids for each of the in-use events, or zero if not used.
  // These are passed in from the driver and then written to the buffer,
  // but otherwise have no meaning to us.
  // All in-use entries appear consecutively.
  PmuEventId fixed_events[IPM_MAX_FIXED_COUNTERS] = {};
  PmuEventId programmable_events[IPM_MAX_PROGRAMMABLE_COUNTERS] = {};
  PmuEventId misc_events[IPM_MAX_MISC_EVENTS] = {};

  // The counters are reset to this at the start.
  // And again for those that are reset on overflow.
  uint64_t fixed_initial_value[IPM_MAX_FIXED_COUNTERS] = {};
  uint64_t programmable_initial_value[IPM_MAX_PROGRAMMABLE_COUNTERS] = {};

  // Flags for each event/counter, perfmon::kPmuConfigFlag*.
  uint32_t fixed_flags[IPM_MAX_FIXED_COUNTERS] = {};
  uint32_t programmable_flags[IPM_MAX_PROGRAMMABLE_COUNTERS] = {};
  uint32_t misc_flags[IPM_MAX_MISC_EVENTS] = {};

  // IA32_PERFEVTSEL_*
  uint64_t programmable_hw_events[IPM_MAX_PROGRAMMABLE_COUNTERS] = {};
};

namespace {
DECLARE_SINGLETON_MUTEX(PerfmonLock);
}  // namespace

static ktl::unique_ptr<PerfmonState> perfmon_state TA_GUARDED(PerfmonLock::Get());

static inline bool x86_perfmon_lbr_is_supported() { return perfmon_lbr_stack_size > 0; }

static inline void enable_counters(PerfmonState* state) {
  write_msr(IA32_PERF_GLOBAL_CTRL, state->global_ctrl);
}

static inline void disable_counters() { write_msr(IA32_PERF_GLOBAL_CTRL, 0); }

zx_status_t PerfmonState::Create(unsigned n_cpus, ktl::unique_ptr<PerfmonState>* out_state) {
  fbl::AllocChecker ac;
  auto state = ktl::unique_ptr<PerfmonState>(new (&ac) PerfmonState(n_cpus));
  if (!ac.check())
    return ZX_ERR_NO_MEMORY;

  if (!state->AllocatePerCpuData()) {
    return ZX_ERR_NO_MEMORY;
  }

  *out_state = ktl::move(state);
  return ZX_OK;
}

PerfmonState::PerfmonState(unsigned n_cpus) : PerfmonStateBase(n_cpus) {}

static bool x86_perfmon_have_mchbar_data() {
  uint32_t vendor_id, device_id;

  auto status = Pci::PioCfgRead(0, 0, 0, PCI_CONFIG_VENDOR_ID, &vendor_id, 16);
  if (status != ZX_OK)
    return false;
  if (vendor_id != INTEL_MCHBAR_PCI_VENDOR_ID)
    return false;
  status = Pci::PioCfgRead(0, 0, 0, PCI_CONFIG_DEVICE_ID, &device_id, 16);
  if (status != ZX_OK)
    return false;
  for (auto supported_device_id : supported_mem_device_ids) {
    if (supported_device_id == device_id)
      return true;
  }

  TRACEF("perfmon: unsupported pci device: 0x%x.0x%x\n", vendor_id, device_id);
  return false;
}

static void x86_perfmon_init_mchbar() {
  uint32_t bar;
  auto status = Pci::PioCfgRead(0, 0, 0, INTEL_MCHBAR_PCI_CONFIG_OFFSET, &bar, 32);
  if (status == ZX_OK) {
    LTRACEF("perfmon: mchbar: 0x%x\n", bar);
    // TODO(dje): The lower four bits contain useful data, but punt for now.
    // See PCI spec 6.2.5.1.
    perfmon_mchbar_bar = bar & ~15u;
    perfmon_num_misc_events = static_cast<uint16_t>(ktl::size(ArchPmuConfig{}.misc_events));
  } else {
    TRACEF("perfmon: error %d reading mchbar\n", status);
  }
}

// Return the size of the LBR stack, or zero if not supported.
static unsigned x86_perfmon_lbr_stack_size() {
  static const struct {
    x86_microarch_list microarch;
    uint8_t stack_size;
  } supported_chips[] = {
      {X86_MICROARCH_INTEL_SKYLAKE, 32},
  };

  unsigned lbr_format = perfmon_capabilities & ((1u << IA32_PERF_CAPABILITIES_LBR_FORMAT_LEN) - 1);
  // TODO(dje): KISS and only support these formats for now.
  switch (lbr_format) {
    case LBR_FORMAT_INFO:
      break;
    default:
      return 0;
  }

  for (const auto& chip : supported_chips) {
    if (chip.microarch == x86_get_microarch_config()->x86_microarch)
      return chip.stack_size;
  }

  return 0;
}

static void x86_perfmon_init_lbr(uint32_t lbr_stack_size) {
  perfmon_lbr_stack_size = lbr_stack_size;
}

static void x86_perfmon_init_once(uint level) {
  struct cpuid_leaf leaf;
  if (!x86_get_cpuid_subleaf(X86_CPUID_PERFORMANCE_MONITORING, 0, &leaf)) {
    return;
  }

  perfmon_version = leaf.a & 0xff;

  perfmon_num_programmable_counters = (leaf.a >> 8) & 0xff;
  if (perfmon_num_programmable_counters > IPM_MAX_PROGRAMMABLE_COUNTERS) {
    TRACEF("perfmon: unexpected num programmable counters %u in cpuid.0AH\n",
           perfmon_num_programmable_counters);
    return;
  }
  perfmon_programmable_counter_width = (leaf.a >> 16) & 0xff;
  // The <16 test is just something simple to ensure it's usable.
  if (perfmon_programmable_counter_width < 16 || perfmon_programmable_counter_width > 64) {
    TRACEF("perfmon: unexpected programmable counter width %u in cpuid.0AH\n",
           perfmon_programmable_counter_width);
    return;
  }
  perfmon_max_programmable_counter_value = ~0ul;
  if (perfmon_programmable_counter_width < 64) {
    perfmon_max_programmable_counter_value = (1ul << perfmon_programmable_counter_width) - 1;
  }

  unsigned ebx_length = (leaf.a >> 24) & 0xff;
  if (ebx_length > 7) {
    TRACEF("perfmon: unexpected value %u in cpuid.0AH.EAH[31..24]\n", ebx_length);
    return;
  }
  perfmon_unsupported_events = leaf.b & ((1u << ebx_length) - 1);

  perfmon_num_fixed_counters = leaf.d & 0x1f;
  if (perfmon_num_fixed_counters > IPM_MAX_FIXED_COUNTERS) {
    TRACEF("perfmon: unexpected num fixed counters %u in cpuid.0AH\n", perfmon_num_fixed_counters);
    return;
  }
  perfmon_fixed_counter_width = (leaf.d >> 5) & 0xff;
  // The <16 test is just something simple to ensure it's usable.
  if (perfmon_fixed_counter_width < 16 || perfmon_fixed_counter_width > 64) {
    TRACEF("perfmon: unexpected fixed counter width %u in cpuid.0AH\n",
           perfmon_fixed_counter_width);
    return;
  }
  perfmon_max_fixed_counter_value = ~0ul;
  if (perfmon_fixed_counter_width < 64) {
    perfmon_max_fixed_counter_value = (1ul << perfmon_fixed_counter_width) - 1;
  }

  perfmon_supported = perfmon_version >= MINIMUM_INTEL_PERFMON_VERSION;

  if (x86_feature_test(X86_FEATURE_PDCM)) {
    perfmon_capabilities = static_cast<uint32_t>(read_msr(IA32_PERF_CAPABILITIES));
  }

  perfmon_counter_status_bits = 0;
  for (unsigned i = 0; i < perfmon_num_programmable_counters; ++i)
    perfmon_counter_status_bits |= IA32_PERF_GLOBAL_STATUS_PMC_OVF_MASK(i);
  for (unsigned i = 0; i < perfmon_num_fixed_counters; ++i)
    perfmon_counter_status_bits |= IA32_PERF_GLOBAL_STATUS_FIXED_OVF_MASK(i);

  kGlobalCtrlWritableBits = 0;
  for (unsigned i = 0; i < perfmon_num_programmable_counters; ++i)
    kGlobalCtrlWritableBits |= IA32_PERF_GLOBAL_CTRL_PMC_EN_MASK(i);
  for (unsigned i = 0; i < perfmon_num_fixed_counters; ++i)
    kGlobalCtrlWritableBits |= IA32_PERF_GLOBAL_CTRL_FIXED_EN_MASK(i);
  kFixedCounterCtrlWritableBits = 0;
  for (unsigned i = 0; i < perfmon_num_fixed_counters; ++i) {
    kFixedCounterCtrlWritableBits |= IA32_FIXED_CTR_CTRL_EN_MASK(i);
    kFixedCounterCtrlWritableBits |= IA32_FIXED_CTR_CTRL_ANY_MASK(i);
    kFixedCounterCtrlWritableBits |= IA32_FIXED_CTR_CTRL_PMI_MASK(i);
  }

  if (x86_perfmon_have_mchbar_data()) {
    x86_perfmon_init_mchbar();
  }

  unsigned lbr_stack_size = x86_perfmon_lbr_stack_size();
  if (lbr_stack_size != 0) {
    // Don't crash if the h/w supports more than we do, just clip it.
    if (lbr_stack_size > perfmon::LastBranchRecord::kMaxNumLastBranch) {
      TRACEF("WARNING: H/W LBR stack size is %u, clipping to %u\n", lbr_stack_size,
             perfmon::LastBranchRecord::kMaxNumLastBranch);
      lbr_stack_size = perfmon::LastBranchRecord::kMaxNumLastBranch;
    }
    x86_perfmon_init_lbr(lbr_stack_size);
  }

  printf("PMU: version %u\n", perfmon_version);
}

LK_INIT_HOOK(x86_perfmon, x86_perfmon_init_once, LK_INIT_LEVEL_ARCH)

static void x86_perfmon_clear_overflow_indicators() {
  uint64_t value = (IA32_PERF_GLOBAL_OVF_CTRL_CLR_COND_CHGD_MASK |
                    IA32_PERF_GLOBAL_OVF_CTRL_DS_BUFFER_CLR_OVF_MASK |
                    IA32_PERF_GLOBAL_OVF_CTRL_UNCORE_CLR_OVF_MASK);

  // This function isn't performance critical enough to precompute this.
  for (unsigned i = 0; i < perfmon_num_programmable_counters; ++i) {
    value |= IA32_PERF_GLOBAL_OVF_CTRL_PMC_CLR_OVF_MASK(i);
  }

  for (unsigned i = 0; i < perfmon_num_fixed_counters; ++i) {
    value |= IA32_PERF_GLOBAL_OVF_CTRL_FIXED_CTR_CLR_OVF_MASK(i);
  }

  write_msr(IA32_PERF_GLOBAL_OVF_CTRL, value);
}

// Return the h/w register number for fixed event id |id|
// or IPM_MAX_FIXED_COUNTERS if not found.
static unsigned x86_perfmon_lookup_fixed_counter(PmuEventId id) {
  if (perfmon::GetEventIdGroup(id) != perfmon::kGroupFixed)
    return IPM_MAX_FIXED_COUNTERS;
  switch (perfmon::GetEventIdEvent(id)) {
#define DEF_FIXED_EVENT(symbol, event_name, id, regnum, flags, readable_name, description) \
  case id:                                                                                 \
    return regnum;
#include <lib/zircon-internal/device/cpu-trace/intel-pm-events.inc>
    default:
      return IPM_MAX_FIXED_COUNTERS;
  }
}

size_t get_max_space_needed_for_all_records(PerfmonState* state) {
  size_t num_events = (state->num_used_programmable + state->num_used_fixed + state->num_used_misc);
  size_t space_needed = (sizeof(perfmon::TimeRecord) + num_events * kMaxEventRecordSize);
  if (state->request_lbr_record)
    space_needed += sizeof(perfmon::LastBranchRecord);
  return space_needed;
}

zx_status_t arch_perfmon_get_properties(ArchPmuProperties* props) {
  Guard<Mutex> guard(PerfmonLock::Get());

  if (!perfmon_supported)
    return ZX_ERR_NOT_SUPPORTED;

  *props = {};
  props->common.pm_version = perfmon_version;
  props->common.max_num_fixed_events = perfmon_num_fixed_counters;
  props->common.max_num_programmable_events = perfmon_num_programmable_counters;
  props->common.max_num_misc_events = perfmon_num_misc_events;
  props->common.max_fixed_counter_width = perfmon_fixed_counter_width;
  props->common.max_programmable_counter_width = perfmon_programmable_counter_width;
  props->common.max_misc_counter_width = perfmon_misc_counter_width;
  props->perf_capabilities = perfmon_capabilities;
  props->lbr_stack_size = perfmon_lbr_stack_size;

  return ZX_OK;
}

zx_status_t arch_perfmon_init() {
  Guard<Mutex> guard(PerfmonLock::Get());

  if (!perfmon_supported)
    return ZX_ERR_NOT_SUPPORTED;
  if (atomic_load(&perfmon_active))
    return ZX_ERR_BAD_STATE;
  if (perfmon_state)
    return ZX_ERR_BAD_STATE;

  ktl::unique_ptr<PerfmonState> state;
  auto status = PerfmonState::Create(arch_max_num_cpus(), &state);
  if (status != ZX_OK)
    return status;

  perfmon_state = ktl::move(state);
  return ZX_OK;
}

zx_status_t arch_perfmon_assign_buffer(uint32_t cpu, fbl::RefPtr<VmObject> vmo) {
  Guard<Mutex> guard(PerfmonLock::Get());

  if (!perfmon_supported)
    return ZX_ERR_NOT_SUPPORTED;
  if (atomic_load(&perfmon_active))
    return ZX_ERR_BAD_STATE;
  if (!perfmon_state)
    return ZX_ERR_BAD_STATE;
  if (cpu >= perfmon_state->num_cpus)
    return ZX_ERR_INVALID_ARGS;

  // A simple safe approximation of the minimum size needed.
  size_t min_size_needed = sizeof(perfmon::BufferHeader);
  min_size_needed += sizeof(perfmon::TimeRecord);
  min_size_needed += perfmon::kMaxNumEvents * kMaxEventRecordSize;
  if (vmo->size() < min_size_needed)
    return ZX_ERR_INVALID_ARGS;

  auto data = &perfmon_state->cpu_data[cpu];
  data->buffer_vmo = vmo;
  data->buffer_size = vmo->size();
  // The buffer is mapped into kernelspace later.

  return ZX_OK;
}

static zx_status_t x86_perfmon_verify_control_config(const ArchPmuConfig* config) {
#if TRY_FREEZE_ON_PMI
  if (!(config->debug_ctrl & IA32_DEBUGCTL_FREEZE_PERFMON_ON_PMI_MASK)) {
    // IWBN to pass back a hint, instead of either nothing or
    // a log message.
    TRACEF("IA32_DEBUGCTL_FREEZE_PERFMON_ON_PMI not set\n");
    return ZX_ERR_INVALID_ARGS;
  }
#else
  if (config->debug_ctrl & IA32_DEBUGCTL_FREEZE_PERFMON_ON_PMI_MASK) {
    TRACEF("IA32_DEBUGCTL_FREEZE_PERFMON_ON_PMI is set\n");
    return ZX_ERR_INVALID_ARGS;
  }
#endif

  if (config->global_ctrl & ~kGlobalCtrlWritableBits) {
    TRACEF("Non writable bits set in |global_ctrl|\n");
    return ZX_ERR_INVALID_ARGS;
  }
  if (config->fixed_ctrl & ~kFixedCounterCtrlWritableBits) {
    TRACEF("Non writable bits set in |fixed_ctrl|\n");
    return ZX_ERR_INVALID_ARGS;
  }
  if (config->debug_ctrl & ~kDebugCtrlWritableBits) {
    TRACEF("Non writable bits set in |debug_ctrl|\n");
    return ZX_ERR_INVALID_ARGS;
  }

  return ZX_OK;
}

static zx_status_t x86_perfmon_verify_fixed_config(const ArchPmuConfig* config,
                                                   unsigned* out_num_used) {
  bool seen_last = false;
  unsigned num_used = perfmon_num_fixed_counters;
  for (unsigned i = 0; i < perfmon_num_fixed_counters; ++i) {
    PmuEventId id = config->fixed_events[i];
    if (id != 0 && seen_last) {
      TRACEF("Active fixed events not front-filled\n");
      return ZX_ERR_INVALID_ARGS;
    }
    // As a rule this file is agnostic to event ids, it's the device
    // driver's job to map them to values we use. Thus we don't
    // validate the ID here. We are given it so that we can include
    // this ID in the trace output.
    if (id == 0) {
      if (!seen_last)
        num_used = i;
      seen_last = true;
    }
    if (seen_last) {
      if (config->fixed_initial_value[i] != 0) {
        TRACEF("Unused |fixed_initial_value[%u]| not zero\n", i);
        return ZX_ERR_INVALID_ARGS;
      }
      if (config->fixed_flags[i] != 0) {
        TRACEF("Unused |fixed_flags[%u]| not zero\n", i);
        return ZX_ERR_INVALID_ARGS;
      }
    } else {
      if (config->fixed_initial_value[i] > perfmon_max_fixed_counter_value) {
        TRACEF("Initial value too large for |fixed_initial_value[%u]|\n", i);
        return ZX_ERR_INVALID_ARGS;
      }
      if (config->fixed_flags[i] & ~perfmon::kPmuConfigFlagMask) {
        TRACEF("Unused bits set in |fixed_flags[%u]|\n", i);
        return ZX_ERR_INVALID_ARGS;
      }
      if (!x86_perfmon_lbr_is_supported() &&
          (config->fixed_flags[i] & perfmon::kPmuConfigFlagLastBranch) != 0) {
        TRACEF(
            "Last branch records requested for |fixed_flags[%u]|,"
            " but not supported\n",
            i);
        return ZX_ERR_NOT_SUPPORTED;
      }
      if ((config->fixed_flags[i] & perfmon::kPmuConfigFlagUsesTimebase) &&
          config->timebase_event == perfmon::kEventIdNone) {
        TRACEF("Timebase requested for |fixed_flags[%u]|, but not provided\n", i);
        return ZX_ERR_INVALID_ARGS;
      }
      unsigned hw_regnum = x86_perfmon_lookup_fixed_counter(id);
      if (hw_regnum == IPM_MAX_FIXED_COUNTERS) {
        TRACEF("Invalid fixed counter id |fixed_events[%u]|\n", i);
        return ZX_ERR_INVALID_ARGS;
      }
    }
  }

  *out_num_used = num_used;
  return ZX_OK;
}

static zx_status_t x86_perfmon_verify_programmable_config(const ArchPmuConfig* config,
                                                          unsigned* out_num_used) {
  bool seen_last = false;
  unsigned num_used = perfmon_num_programmable_counters;
  for (unsigned i = 0; i < perfmon_num_programmable_counters; ++i) {
    PmuEventId id = config->programmable_events[i];
    if (id != 0 && seen_last) {
      TRACEF("Active programmable events not front-filled\n");
      return ZX_ERR_INVALID_ARGS;
    }
    // As a rule this file is agnostic to event ids, it's the device
    // driver's job to map them to the hw values we use. Thus we don't
    // validate the ID here. We are given it so that we can include
    // this ID in the trace output.
    if (id == 0) {
      if (!seen_last)
        num_used = i;
      seen_last = true;
    }
    if (seen_last) {
      if (config->programmable_hw_events[i] != 0) {
        TRACEF("Unused |programmable_hw_events[%u]| not zero\n", i);
        return ZX_ERR_INVALID_ARGS;
      }
      if (config->programmable_initial_value[i] != 0) {
        TRACEF("Unused |programmable_initial_value[%u]| not zero\n", i);
        return ZX_ERR_INVALID_ARGS;
      }
      if (config->programmable_flags[i] != 0) {
        TRACEF("Unused |programmable_flags[%u]| not zero\n", i);
        return ZX_ERR_INVALID_ARGS;
      }
    } else {
      if (config->programmable_hw_events[i] & ~kEventSelectWritableBits) {
        TRACEF("Non writable bits set in |programmable_hw_events[%u]|\n", i);
        return ZX_ERR_INVALID_ARGS;
      }
      if (config->programmable_initial_value[i] > perfmon_max_programmable_counter_value) {
        TRACEF("Initial value too large for |programmable_initial_value[%u]|\n", i);
        return ZX_ERR_INVALID_ARGS;
      }
      if (config->programmable_flags[i] & ~perfmon::kPmuConfigFlagMask) {
        TRACEF("Unused bits set in |programmable_flags[%u]|\n", i);
        return ZX_ERR_INVALID_ARGS;
      }
      if (!x86_perfmon_lbr_is_supported() &&
          (config->programmable_flags[i] & perfmon::kPmuConfigFlagLastBranch) != 0) {
        TRACEF(
            "Last branch records requested for |programmable_flags[%u]|,"
            " but not supported\n",
            i);
        return ZX_ERR_NOT_SUPPORTED;
      }
      if ((config->programmable_flags[i] & perfmon::kPmuConfigFlagUsesTimebase) &&
          config->timebase_event == perfmon::kEventIdNone) {
        TRACEF("Timebase requested for |programmable_flags[%u]|, but not provided\n", i);
        return ZX_ERR_INVALID_ARGS;
      }
    }
  }

  *out_num_used = num_used;
  return ZX_OK;
}

static zx_status_t x86_perfmon_verify_misc_config(const ArchPmuConfig* config,
                                                  unsigned* out_num_used) {
  bool seen_last = false;
  size_t max_num_used = ktl::size(config->misc_events);
  size_t num_used = max_num_used;
  for (size_t i = 0; i < max_num_used; ++i) {
    PmuEventId id = config->misc_events[i];
    if (id != 0 && seen_last) {
      TRACEF("Active misc events not front-filled\n");
      return ZX_ERR_INVALID_ARGS;
    }
    if (id == 0) {
      if (!seen_last)
        num_used = i;
      seen_last = true;
    }
    if (seen_last) {
      if (config->misc_flags[i] != 0) {
        TRACEF("Unused |misc_flags[%zu]| not zero\n", i);
        return ZX_ERR_INVALID_ARGS;
      }
    } else {
      if (config->misc_flags[i] & ~perfmon::kPmuConfigFlagMask) {
        TRACEF("Unused bits set in |misc_flags[%zu]|\n", i);
        return ZX_ERR_INVALID_ARGS;
      }
      // Currently we only support the MCHBAR events.
      // They cannot provide pc. We ignore the OS/USER bits.
      if (config->misc_flags[i] & (perfmon::kPmuConfigFlagPc | perfmon::kPmuConfigFlagLastBranch)) {
        TRACEF("Invalid bits (0x%x) in |misc_flags[%zu]|\n", config->misc_flags[i], i);
        return ZX_ERR_INVALID_ARGS;
      }
      if ((config->misc_flags[i] & perfmon::kPmuConfigFlagUsesTimebase) &&
          config->timebase_event == perfmon::kEventIdNone) {
        TRACEF("Timebase requested for |misc_flags[%zu]|, but not provided\n", i);
        return ZX_ERR_INVALID_ARGS;
      }
      switch (perfmon::GetEventIdEvent(id)) {
#define DEF_MISC_SKL_EVENT(symbol, event_name, id, offset, size, flags, readable_name, \
                           description)                                                \
  case id:                                                                             \
    break;
#include <lib/zircon-internal/device/cpu-trace/skylake-misc-events.inc>
        default:
          TRACEF("Invalid misc event id |misc_events[%zu]|\n", i);
          return ZX_ERR_INVALID_ARGS;
      }
    }
  }

  *out_num_used = static_cast<unsigned>(num_used);
  return ZX_OK;
}

static zx_status_t x86_perfmon_verify_timebase_config(ArchPmuConfig* config, unsigned num_fixed,
                                                      unsigned num_programmable) {
  if (config->timebase_event == perfmon::kEventIdNone) {
    return ZX_OK;
  }

  for (unsigned i = 0; i < num_fixed; ++i) {
    if (config->fixed_events[i] == config->timebase_event) {
      // The PMI code is simpler if this is the case.
      config->fixed_flags[i] &= ~perfmon::kPmuConfigFlagUsesTimebase;
      return ZX_OK;
    }
  }

  for (unsigned i = 0; i < num_programmable; ++i) {
    if (config->programmable_events[i] == config->timebase_event) {
      // The PMI code is simpler if this is the case.
      config->programmable_flags[i] &= ~perfmon::kPmuConfigFlagUsesTimebase;
      return ZX_OK;
    }
  }

  TRACEF("Timebase 0x%x requested but not present\n", config->timebase_event);
  return ZX_ERR_INVALID_ARGS;
}

static zx_status_t x86_perfmon_verify_config(ArchPmuConfig* config, PerfmonState* state) {
  auto status = x86_perfmon_verify_control_config(config);
  if (status != ZX_OK)
    return status;

  unsigned num_used_fixed;
  status = x86_perfmon_verify_fixed_config(config, &num_used_fixed);
  if (status != ZX_OK)
    return status;
  state->num_used_fixed = num_used_fixed;

  unsigned num_used_programmable;
  status = x86_perfmon_verify_programmable_config(config, &num_used_programmable);
  if (status != ZX_OK)
    return status;
  state->num_used_programmable = num_used_programmable;

  unsigned num_used_misc;
  status = x86_perfmon_verify_misc_config(config, &num_used_misc);
  if (status != ZX_OK)
    return status;
  state->num_used_misc = num_used_misc;

  status = x86_perfmon_verify_timebase_config(config, state->num_used_fixed,
                                              state->num_used_programmable);
  if (status != ZX_OK)
    return status;

  return ZX_OK;
}

static void x86_perfmon_stage_fixed_config(const ArchPmuConfig* config, PerfmonState* state) {
  static_assert(sizeof(state->fixed_events) == sizeof(config->fixed_events), "");
  memcpy(state->fixed_events, config->fixed_events, sizeof(state->fixed_events));

  static_assert(sizeof(state->fixed_initial_value) == sizeof(config->fixed_initial_value), "");
  memcpy(state->fixed_initial_value, config->fixed_initial_value,
         sizeof(state->fixed_initial_value));

  static_assert(sizeof(state->fixed_flags) == sizeof(config->fixed_flags), "");
  memcpy(state->fixed_flags, config->fixed_flags, sizeof(state->fixed_flags));

  for (unsigned i = 0; i < ktl::size(state->fixed_hw_map); ++i) {
    state->fixed_hw_map[i] = x86_perfmon_lookup_fixed_counter(config->fixed_events[i]);
  }
}

static void x86_perfmon_stage_programmable_config(const ArchPmuConfig* config,
                                                  PerfmonState* state) {
  static_assert(sizeof(state->programmable_events) == sizeof(config->programmable_events), "");
  memcpy(state->programmable_events, config->programmable_events,
         sizeof(state->programmable_events));

  static_assert(
      sizeof(state->programmable_initial_value) == sizeof(config->programmable_initial_value), "");
  memcpy(state->programmable_initial_value, config->programmable_initial_value,
         sizeof(state->programmable_initial_value));

  static_assert(sizeof(state->programmable_flags) == sizeof(config->programmable_flags), "");
  memcpy(state->programmable_flags, config->programmable_flags, sizeof(state->programmable_flags));

  static_assert(sizeof(state->programmable_hw_events) == sizeof(config->programmable_hw_events),
                "");
  memcpy(state->programmable_hw_events, config->programmable_hw_events,
         sizeof(state->programmable_hw_events));
}

static void x86_perfmon_stage_misc_config(const ArchPmuConfig* config, PerfmonState* state) {
  static_assert(sizeof(state->misc_events) == sizeof(config->misc_events), "");
  memcpy(state->misc_events, config->misc_events, sizeof(state->misc_events));

  static_assert(sizeof(state->misc_flags) == sizeof(config->misc_flags), "");
  memcpy(state->misc_flags, config->misc_flags, sizeof(state->misc_flags));

  state->need_mchbar = false;
  for (unsigned i = 0; i < state->num_used_misc; ++i) {
    // All misc events currently come from MCHBAR.
    // When needed we can add a flag to the event to denote origin.
    switch (perfmon::GetEventIdEvent(state->misc_events[i])) {
#define DEF_MISC_SKL_EVENT(symbol, event_name, id, offset, size, flags, readable_name, \
                           description)                                                \
  case id:
#include <lib/zircon-internal/device/cpu-trace/skylake-misc-events.inc>
      state->need_mchbar = true;
      break;
      default:
        break;
    }
  }

  // What we'd like to do here is record the current values of these
  // events, but they're not mapped in yet.
  memset(&state->mchbar_data.last_mem, 0, sizeof(state->mchbar_data.last_mem));
}

// Stage the configuration for later activation by START.
// One of the main goals of this function is to verify the provided config
// is ok, e.g., it won't cause us to crash.
zx_status_t arch_perfmon_stage_config(ArchPmuConfig* config) {
  Guard<Mutex> guard(PerfmonLock::Get());

  if (!perfmon_supported)
    return ZX_ERR_NOT_SUPPORTED;
  if (atomic_load(&perfmon_active))
    return ZX_ERR_BAD_STATE;
  if (!perfmon_state)
    return ZX_ERR_BAD_STATE;

  auto state = perfmon_state.get();

  LTRACEF("global_ctrl 0x%" PRIx64 "\n", config->global_ctrl);

  // Note: The verification pass may also alter |config| to make things
  // simpler for the implementation.
  auto status = x86_perfmon_verify_config(config, state);
  if (status != ZX_OK)
    return status;

  state->global_ctrl = config->global_ctrl;
  state->fixed_ctrl = config->fixed_ctrl;
  state->debug_ctrl = config->debug_ctrl;
  state->timebase_event = config->timebase_event;

  if (state->debug_ctrl & IA32_DEBUGCTL_LBR_MASK) {
    if (!x86_perfmon_lbr_is_supported()) {
      TRACEF("Last branch records requested in |debug_ctrl|, but not supported\n");
      return ZX_ERR_NOT_SUPPORTED;
    }
    state->request_lbr_record = true;
  }

  x86_perfmon_stage_fixed_config(config, state);
  x86_perfmon_stage_programmable_config(config, state);
  x86_perfmon_stage_misc_config(config, state);

  return ZX_OK;
}

// System statistics that come from MCHBAR.
// See, e.g., desktop-6th-gen-core-family-datasheet-vol-2.
// TODO(dje): Consider moving misc event support to a separate file
// when the amount of code to support them gets large enough.

// Take advantage of the ABI's support for returning two values so that
// we can return both in registers.
struct ReadMiscResult {
  // The value of the register.
  uint64_t value;
  // The record type to use, either |perfmon::kRecordTypeCount| or
  // |perfmon::kRecordTypeValue|.
  uint8_t type;
};

// Read the 32-bit counter from MCHBAR and return the delta
// since the last read. We do this in part because it's easier for clients
// to process and in part to catch the cases of the counter wrapping that
// we can (they're only 32 bits in h/w and are read-only).
// WARNING: This function has the side-effect of updating |*last_value|.
static uint32_t read_mc_counter32(volatile uint32_t* addr, uint32_t* last_value_addr) {
  uint32_t value = *addr;
  uint32_t last_value = *last_value_addr;
  *last_value_addr = value;
  // Check for overflow. The code is the same in both branches, the if()
  // exists to document the issue.
  if (value < last_value) {
    // Overflow, counter wrapped.
    // We don't know how many times it wrapped, assume once.
    // We rely on unsigned twos-complement arithmetic here.
    return value - last_value;
  } else {
    // The counter may still have wrapped, but we can't detect this case.
    return value - last_value;
  }
}

// Read the 64-bit counter from MCHBAR and return the delta
// since the last read. We do this because it's easier for clients to process.
// Overflow is highly unlikely with a 64-bit counter.
// WARNING: This function has the side-effect of updating |*last_value|.
static uint64_t read_mc_counter64(volatile uint64_t* addr, uint64_t* last_value_addr) {
  uint64_t value = *addr;
  uint64_t last_value = *last_value_addr;
  *last_value_addr = value;
  return value - last_value;
}

// Read the 32-bit non-counter value from MCHBAR.
static uint32_t read_mc_value32(volatile uint32_t* addr) { return *addr; }

static ReadMiscResult read_mc_typed_counter32(volatile uint32_t* addr, uint32_t* last_value_addr) {
  return ReadMiscResult{read_mc_counter32(addr, last_value_addr), perfmon::kRecordTypeCount};
}

static ReadMiscResult read_mc_typed_counter64(volatile uint64_t* addr, uint64_t* last_value_addr) {
  return ReadMiscResult{read_mc_counter64(addr, last_value_addr), perfmon::kRecordTypeCount};
}

static ReadMiscResult read_mc_typed_value32(volatile uint32_t* addr) {
  return ReadMiscResult{read_mc_value32(addr), perfmon::kRecordTypeValue};
}

static volatile uint32_t* get_mc_addr32(PerfmonState* state, uint32_t hw_addr) {
  return reinterpret_cast<volatile uint32_t*>(
      reinterpret_cast<volatile char*>(state->mchbar_data.stats_addr) + hw_addr -
      UNC_IMC_STATS_BEGIN);
}

static volatile uint64_t* get_mc_addr64(PerfmonState* state, uint32_t hw_addr) {
  return reinterpret_cast<volatile uint64_t*>(
      reinterpret_cast<volatile char*>(state->mchbar_data.stats_addr) + hw_addr -
      UNC_IMC_STATS_BEGIN);
}

static ReadMiscResult read_mc_bytes_read(PerfmonState* state) {
  uint32_t value = read_mc_counter32(get_mc_addr32(state, MISC_MEM_BYTES_READ_OFFSET),
                                     &state->mchbar_data.last_mem.bytes_read);
  // Return the value in bytes, easier for human readers of the
  // resulting report.
  return ReadMiscResult{value * 64ul, perfmon::kRecordTypeCount};
}

static ReadMiscResult read_mc_bytes_written(PerfmonState* state) {
  uint32_t value = read_mc_counter32(get_mc_addr32(state, MISC_MEM_BYTES_WRITTEN_OFFSET),
                                     &state->mchbar_data.last_mem.bytes_written);
  // Return the value in bytes, easier for human readers of the
  // resulting report.
  return ReadMiscResult{value * 64ul, perfmon::kRecordTypeCount};
}

static ReadMiscResult read_mc_gt_requests(PerfmonState* state) {
  return read_mc_typed_counter32(get_mc_addr32(state, MISC_MEM_GT_REQUESTS_OFFSET),
                                 &state->mchbar_data.last_mem.gt_requests);
}

static ReadMiscResult read_mc_ia_requests(PerfmonState* state) {
  return read_mc_typed_counter32(get_mc_addr32(state, MISC_MEM_IA_REQUESTS_OFFSET),
                                 &state->mchbar_data.last_mem.ia_requests);
}

static ReadMiscResult read_mc_io_requests(PerfmonState* state) {
  return read_mc_typed_counter32(get_mc_addr32(state, MISC_MEM_IO_REQUESTS_OFFSET),
                                 &state->mchbar_data.last_mem.io_requests);
}

static ReadMiscResult read_mc_all_active_core_cycles(PerfmonState* state) {
  return read_mc_typed_counter64(get_mc_addr64(state, MISC_PKG_ALL_ACTIVE_CORE_CYCLES_OFFSET),
                                 &state->mchbar_data.last_mem.all_active_core_cycles);
}

static ReadMiscResult read_mc_any_active_core_cycles(PerfmonState* state) {
  return read_mc_typed_counter64(get_mc_addr64(state, MISC_PKG_ANY_ACTIVE_CORE_CYCLES_OFFSET),
                                 &state->mchbar_data.last_mem.any_active_core_cycles);
}

static ReadMiscResult read_mc_active_gt_cycles(PerfmonState* state) {
  return read_mc_typed_counter64(get_mc_addr64(state, MISC_PKG_ACTIVE_GT_CYCLES_OFFSET),
                                 &state->mchbar_data.last_mem.active_gt_cycles);
}

static ReadMiscResult read_mc_active_ia_gt_cycles(PerfmonState* state) {
  return read_mc_typed_counter64(get_mc_addr64(state, MISC_PKG_ACTIVE_IA_GT_CYCLES_OFFSET),
                                 &state->mchbar_data.last_mem.active_ia_gt_cycles);
}

static ReadMiscResult read_mc_active_gt_slice_cycles(PerfmonState* state) {
  return read_mc_typed_counter64(get_mc_addr64(state, MISC_PKG_ACTIVE_GT_SLICE_CYCLES_OFFSET),
                                 &state->mchbar_data.last_mem.active_gt_slice_cycles);
}

static ReadMiscResult read_mc_active_gt_engine_cycles(PerfmonState* state) {
  return read_mc_typed_counter64(get_mc_addr64(state, MISC_PKG_ACTIVE_GT_ENGINE_CYCLES_OFFSET),
                                 &state->mchbar_data.last_mem.active_gt_engine_cycles);
}

static ReadMiscResult read_mc_peci_therm_margin(PerfmonState* state) {
  uint32_t value = read_mc_value32(get_mc_addr32(state, MISC_PKG_PECI_THERM_MARGIN_OFFSET));
  return ReadMiscResult{value & 0xffff, perfmon::kRecordTypeValue};
}

static ReadMiscResult read_mc_rapl_perf_status(PerfmonState* state) {
  return read_mc_typed_value32(get_mc_addr32(state, MISC_PKG_RAPL_PERF_STATUS_OFFSET));
}

static ReadMiscResult read_mc_ia_freq_clamping_reasons(PerfmonState* state) {
  // Some of the reserved bits have read as ones. Remove them to make the
  // reported value easier to read.
  const uint32_t kReserved = (1u << 31) | (1u << 30) | (1u << 25) | (1u << 19) | (1u << 18) |
                             (1u << 15) | (1u << 14) | (1u << 9) | (1u << 3) | (1u << 2);
  uint32_t value = read_mc_value32(get_mc_addr32(state, MISC_PKG_IA_FREQ_CLAMPING_REASONS_OFFSET));
  return ReadMiscResult{value & ~kReserved, perfmon::kRecordTypeValue};
}

static ReadMiscResult read_mc_gt_freq_clamping_reasons(PerfmonState* state) {
  // Some of the reserved bits have read as ones. Remove them to make the
  // reported value easier to read.
  const uint32_t kReserved = (1u << 31) | (1u << 30) | (1u << 29) | (1u << 25) | (1u << 20) |
                             (1u << 19) | (1u << 18) | (1u << 15) | (1u << 14) | (1u << 13) |
                             (1u << 9) | (1u << 4) | (1u << 3) | (1u << 2);
  uint32_t value = read_mc_value32(get_mc_addr32(state, MISC_PKG_GT_FREQ_CLAMPING_REASONS_OFFSET));
  return ReadMiscResult{value & ~kReserved, perfmon::kRecordTypeValue};
}

static ReadMiscResult read_mc_rp_slice_freq(PerfmonState* state) {
  uint32_t value = read_mc_value32(get_mc_addr32(state, MISC_PKG_RP_GT_SLICE_FREQ_OFFSET));
  value = (value >> 17) & 0x1ff;
  // Convert the value to Mhz.
  // We can't do floating point, and this doesn't have to be perfect.
  uint64_t scaled_value = value * 16667ul / 1000 /*16.667*/;
  return ReadMiscResult{scaled_value, perfmon::kRecordTypeValue};
}

static ReadMiscResult read_mc_rp_unslice_freq(PerfmonState* state) {
  uint32_t value = read_mc_value32(get_mc_addr32(state, MISC_PKG_RP_GT_UNSLICE_FREQ_OFFSET));
  value = (value >> 8) & 0x1ff;
  // Convert the value to Mhz.
  // We can't do floating point, and this doesn't have to be perfect.
  uint64_t scaled_value = value * 16667ul / 1000 /*16.667*/;
  return ReadMiscResult{scaled_value, perfmon::kRecordTypeValue};
}

static ReadMiscResult read_mc_rp_gt_volt(PerfmonState* state) {
  uint32_t value = read_mc_value32(get_mc_addr32(state, MISC_PKG_RP_GT_VOLT_OFFSET));
  return ReadMiscResult{value & 0xff, perfmon::kRecordTypeValue};
}

static ReadMiscResult read_mc_edram_temp(PerfmonState* state) {
  uint32_t value = read_mc_value32(get_mc_addr32(state, MISC_PKG_EDRAM_TEMP_OFFSET));
  return ReadMiscResult{value & 0xff, perfmon::kRecordTypeValue};
}

static ReadMiscResult read_mc_pkg_temp(PerfmonState* state) {
  uint32_t value = read_mc_value32(get_mc_addr32(state, MISC_PKG_PKG_TEMP_OFFSET));
  return ReadMiscResult{value & 0xff, perfmon::kRecordTypeValue};
}

static ReadMiscResult read_mc_ia_temp(PerfmonState* state) {
  uint32_t value = read_mc_value32(get_mc_addr32(state, MISC_PKG_IA_TEMP_OFFSET));
  return ReadMiscResult{value & 0xff, perfmon::kRecordTypeValue};
}

static ReadMiscResult read_mc_gt_temp(PerfmonState* state) {
  uint32_t value = read_mc_value32(get_mc_addr32(state, MISC_PKG_GT_TEMP_OFFSET));
  return ReadMiscResult{value & 0xff, perfmon::kRecordTypeValue};
}

static ReadMiscResult read_misc_event(PerfmonState* state, PmuEventId id) {
  switch (id) {
    case MISC_MEM_BYTES_READ_ID:
      return read_mc_bytes_read(state);
    case MISC_MEM_BYTES_WRITTEN_ID:
      return read_mc_bytes_written(state);
    case MISC_MEM_GT_REQUESTS_ID:
      return read_mc_gt_requests(state);
    case MISC_MEM_IA_REQUESTS_ID:
      return read_mc_ia_requests(state);
    case MISC_MEM_IO_REQUESTS_ID:
      return read_mc_io_requests(state);
    case MISC_PKG_ALL_ACTIVE_CORE_CYCLES_ID:
      return read_mc_all_active_core_cycles(state);
    case MISC_PKG_ANY_ACTIVE_CORE_CYCLES_ID:
      return read_mc_any_active_core_cycles(state);
    case MISC_PKG_ACTIVE_GT_CYCLES_ID:
      return read_mc_active_gt_cycles(state);
    case MISC_PKG_ACTIVE_IA_GT_CYCLES_ID:
      return read_mc_active_ia_gt_cycles(state);
    case MISC_PKG_ACTIVE_GT_SLICE_CYCLES_ID:
      return read_mc_active_gt_slice_cycles(state);
    case MISC_PKG_ACTIVE_GT_ENGINE_CYCLES_ID:
      return read_mc_active_gt_engine_cycles(state);
    case MISC_PKG_PECI_THERM_MARGIN_ID:
      return read_mc_peci_therm_margin(state);
    case MISC_PKG_RAPL_PERF_STATUS_ID:
      return read_mc_rapl_perf_status(state);
    case MISC_PKG_IA_FREQ_CLAMPING_REASONS_ID:
      return read_mc_ia_freq_clamping_reasons(state);
    case MISC_PKG_GT_FREQ_CLAMPING_REASONS_ID:
      return read_mc_gt_freq_clamping_reasons(state);
    case MISC_PKG_RP_GT_SLICE_FREQ_ID:
      return read_mc_rp_slice_freq(state);
    case MISC_PKG_RP_GT_UNSLICE_FREQ_ID:
      return read_mc_rp_unslice_freq(state);
    case MISC_PKG_RP_GT_VOLT_ID:
      return read_mc_rp_gt_volt(state);
    case MISC_PKG_EDRAM_TEMP_ID:
      return read_mc_edram_temp(state);
    case MISC_PKG_PKG_TEMP_ID:
      return read_mc_pkg_temp(state);
    case MISC_PKG_IA_TEMP_ID:
      return read_mc_ia_temp(state);
    case MISC_PKG_GT_TEMP_ID:
      return read_mc_gt_temp(state);
    default:
      __UNREACHABLE;
  }
}

static void x86_perfmon_unmap_buffers_locked(PerfmonState* state) {
  unsigned num_cpus = state->num_cpus;
  for (unsigned cpu = 0; cpu < num_cpus; ++cpu) {
    auto data = &state->cpu_data[cpu];
    if (data->buffer_start) {
      data->buffer_mapping->Destroy();
    }
    data->buffer_mapping.reset();
    data->buffer_start = nullptr;
    data->buffer_end = nullptr;
    data->buffer_next = nullptr;
  }

  if (state->mchbar_data.mapping) {
    state->mchbar_data.mapping->Destroy();
  }
  state->mchbar_data.mapping.reset();
  state->mchbar_data.stats_addr = nullptr;

  LTRACEF("buffers unmapped");
}

static zx_status_t x86_map_mchbar_stat_registers(PerfmonState* state) {
  DEBUG_ASSERT(perfmon_mchbar_bar != 0);
  fbl::RefPtr<VmObjectPhysical> vmo;
  vaddr_t begin_page = (perfmon_mchbar_bar + UNC_IMC_STATS_BEGIN) & ~(PAGE_SIZE - 1);
  vaddr_t end_page = (perfmon_mchbar_bar + UNC_IMC_STATS_END) & ~(PAGE_SIZE - 1);
  size_t num_bytes_to_map = end_page + PAGE_SIZE - begin_page;
  size_t begin_offset = (perfmon_mchbar_bar + UNC_IMC_STATS_BEGIN) & (PAGE_SIZE - 1);

  // We only map in the page(s) with the data we need.
  auto status = VmObjectPhysical::Create(begin_page, num_bytes_to_map, &vmo);
  if (status != ZX_OK)
    return status;

  const char name[] = "perfmon-mchbar";
  vmo->set_name(name, sizeof(name));
  status = vmo->SetMappingCachePolicy(ZX_CACHE_POLICY_UNCACHED_DEVICE);
  if (status != ZX_OK)
    return status;

  auto vmar = VmAspace::kernel_aspace()->RootVmar();
  uint32_t vmar_flags = 0;
  uint32_t arch_mmu_flags = ARCH_MMU_FLAG_PERM_READ;
  fbl::RefPtr<VmMapping> mapping;
  status = vmar->CreateVmMapping(0, PAGE_SIZE, /*align_pow2*/ 0, vmar_flags, ktl::move(vmo), 0,
                                 arch_mmu_flags, name, &mapping);
  if (status != ZX_OK)
    return status;

  status = mapping->MapRange(0, PAGE_SIZE, false);
  if (status != ZX_OK)
    return status;

  state->mchbar_data.mapping = mapping;
  state->mchbar_data.stats_addr = reinterpret_cast<void*>(mapping->base() + begin_offset);

  // Record the current values of these so that the trace will only include
  // the delta since tracing started.
#define INIT_MC_COUNT(member)               \
  do {                                      \
    state->mchbar_data.last_mem.member = 0; \
    (void)read_mc_##member(state);          \
  } while (0)
  INIT_MC_COUNT(bytes_read);
  INIT_MC_COUNT(bytes_written);
  INIT_MC_COUNT(gt_requests);
  INIT_MC_COUNT(ia_requests);
  INIT_MC_COUNT(io_requests);
  INIT_MC_COUNT(all_active_core_cycles);
  INIT_MC_COUNT(any_active_core_cycles);
  INIT_MC_COUNT(active_gt_cycles);
  INIT_MC_COUNT(active_ia_gt_cycles);
  INIT_MC_COUNT(active_gt_slice_cycles);
  INIT_MC_COUNT(active_gt_engine_cycles);
#undef INIT_MC_COUNT

  LTRACEF("memory stats mapped: begin 0x%lx, %zu bytes\n", mapping->base(), num_bytes_to_map);

  return ZX_OK;
}

static zx_status_t x86_perfmon_map_buffers_locked(PerfmonState* state) {
  unsigned num_cpus = state->num_cpus;
  zx_status_t status = ZX_OK;
  for (unsigned cpu = 0; cpu < num_cpus; ++cpu) {
    auto data = &state->cpu_data[cpu];
    // Heads up: The logic is off if |vmo_offset| is non-zero.
    const uint64_t vmo_offset = 0;
    const size_t size = data->buffer_size;
    const uint arch_mmu_flags = ARCH_MMU_FLAG_PERM_READ | ARCH_MMU_FLAG_PERM_WRITE;
    const char* name = "ipm-buffer";
    status = VmAspace::kernel_aspace()->RootVmar()->CreateVmMapping(
        0 /* ignored */, size, 0 /* align pow2 */, 0 /* vmar flags */, data->buffer_vmo, vmo_offset,
        arch_mmu_flags, name, &data->buffer_mapping);
    if (status != ZX_OK) {
      TRACEF("error %d mapping buffer: cpu %u, size 0x%zx\n", status, cpu, size);
      break;
    }
    // Pass true for |commit| so that we get our pages mapped up front.
    // Otherwise we'll need to allow for a page fault to happen in the
    // PMI handler.
    status = data->buffer_mapping->MapRange(vmo_offset, size, true);
    if (status != ZX_OK) {
      TRACEF("error %d mapping range: cpu %u, size 0x%zx\n", status, cpu, size);
      data->buffer_mapping->Destroy();
      data->buffer_mapping.reset();
      break;
    }
    data->buffer_start =
        reinterpret_cast<perfmon::BufferHeader*>(data->buffer_mapping->base() + vmo_offset);
    data->buffer_end = reinterpret_cast<char*>(data->buffer_start) + size;
    LTRACEF("buffer mapped: cpu %u, start %p, end %p\n", cpu, data->buffer_start, data->buffer_end);

    auto hdr = data->buffer_start;
    hdr->version = perfmon::kBufferVersion;
    hdr->arch = perfmon::kArchX64;
    hdr->flags = 0;
    hdr->ticks_per_second = ticks_per_second();
    hdr->capture_end = sizeof(*hdr);
    data->buffer_next = reinterpret_cast<perfmon::RecordHeader*>(
        reinterpret_cast<char*>(data->buffer_start) + hdr->capture_end);
  }

  // Get access to MCHBAR stats if we can.
  if (status == ZX_OK && state->need_mchbar) {
    status = x86_map_mchbar_stat_registers(state);
  }

  if (status != ZX_OK) {
    x86_perfmon_unmap_buffers_locked(state);
  }

  return status;
}

static void x86_perfmon_start_cpu_task(void* raw_context) {
  DEBUG_ASSERT(arch_ints_disabled());
  DEBUG_ASSERT(!atomic_load(&perfmon_active) && raw_context);

  auto state = reinterpret_cast<PerfmonState*>(raw_context);

  for (unsigned i = 0; i < state->num_used_fixed; ++i) {
    unsigned hw_num = state->fixed_hw_map[i];
    DEBUG_ASSERT(hw_num < perfmon_num_fixed_counters);
    write_msr(IA32_FIXED_CTR0 + hw_num, state->fixed_initial_value[i]);
  }
  write_msr(IA32_FIXED_CTR_CTRL, state->fixed_ctrl);

  for (unsigned i = 0; i < state->num_used_programmable; ++i) {
    // Ensure PERFEVTSEL.EN is zero before resetting the counter value,
    // h/w requires it (apparently even if global ctrl is off).
    write_msr(IA32_PERFEVTSEL_FIRST + i, 0);
    // The counter must be written before PERFEVTSEL.EN is set to 1.
    write_msr(IA32_PMC_FIRST + i, state->programmable_initial_value[i]);
    write_msr(IA32_PERFEVTSEL_FIRST + i, state->programmable_hw_events[i]);
  }

  write_msr(IA32_DEBUGCTL, state->debug_ctrl);

  apic_pmi_unmask();

  // Enable counters as late as possible so that our setup doesn't contribute
  // to the data.
  enable_counters(state);
}

// Begin collecting data.

zx_status_t arch_perfmon_start() {
  Guard<Mutex> guard(PerfmonLock::Get());

  if (!perfmon_supported)
    return ZX_ERR_NOT_SUPPORTED;
  if (atomic_load(&perfmon_active))
    return ZX_ERR_BAD_STATE;
  if (!perfmon_state)
    return ZX_ERR_BAD_STATE;

  // Make sure all relevant sysregs have been wiped clean.
  if (!perfmon_hw_initialized) {
    mp_sync_exec(MP_IPI_TARGET_ALL, 0, x86_perfmon_reset_task, nullptr);
    perfmon_hw_initialized = true;
  }

  // Sanity check the buffers and map them in.
  // This is deferred until now so that they are mapped in as minimally as
  // necessary.
  // TODO(dje): OTOH one might want to start/stop/start/stop/... and
  // continually mapping/unmapping will be painful. Revisit when things
  // settle down.
  auto state = perfmon_state.get();
  auto status = x86_perfmon_map_buffers_locked(state);
  if (status != ZX_OK)
    return status;

  TRACEF("Enabling perfmon, %u fixed, %u programmable, %u misc\n", state->num_used_fixed,
         state->num_used_programmable, state->num_used_misc);
  if (LOCAL_TRACE) {
    LTRACEF("global ctrl: 0x%" PRIx64 ", fixed ctrl: 0x%" PRIx64 "\n", state->global_ctrl,
            state->fixed_ctrl);
    for (unsigned i = 0; i < state->num_used_fixed; ++i) {
      LTRACEF("fixed[%u]: num %u, initial 0x%" PRIx64 "\n", i, state->fixed_hw_map[i],
              state->fixed_initial_value[i]);
    }
    for (unsigned i = 0; i < state->num_used_programmable; ++i) {
      LTRACEF("programmable[%u]: id 0x%x, initial 0x%" PRIx64 "\n", i,
              state->programmable_events[i], state->programmable_initial_value[i]);
    }
  }

  mp_sync_exec(MP_IPI_TARGET_ALL, 0, x86_perfmon_start_cpu_task, state);
  atomic_store(&perfmon_active, true);

  return ZX_OK;
}

// This is invoked via mp_sync_exec which thread safety analysis cannot follow.
static void x86_perfmon_write_last_records(PerfmonState* state, cpu_num_t cpu) {
  PerfmonCpuData* data = &state->cpu_data[cpu];
  perfmon::RecordHeader* next = data->buffer_next;

  zx_time_t now = _rdtsc();
  next = arch_perfmon_write_time_record(next, perfmon::kEventIdNone, now);

  // If the counter triggers interrupts then the PMI handler will
  // continually reset it to its initial value. To keep things simple
  // just always subtract out the initial value from the current value
  // and write the difference out. For non-interrupt triggering events
  // the user should normally initialize the counter to zero to get
  // correct results.
  // Counters that don't trigger interrupts could overflow and we won't
  // necessarily catch it, but there's nothing we can do about it.
  // We can handle the overflowed-once case, which should catch the
  // vast majority of cases.
  // TODO(dje): Counters that trigger interrupts should never have
  // an overflowed value here, but that's what I'm seeing.

  for (unsigned i = 0; i < state->num_used_programmable; ++i) {
    PmuEventId id = state->programmable_events[i];
    DEBUG_ASSERT(id != 0);
    uint64_t count = read_msr(IA32_PMC_FIRST + i);
    if (count >= state->programmable_initial_value[i]) {
      count -= state->programmable_initial_value[i];
    } else {
      // The max counter value is generally not 64 bits.
      count += (perfmon_max_programmable_counter_value - state->programmable_initial_value[i] + 1);
    }
    next = arch_perfmon_write_count_record(next, id, count);
  }
  for (unsigned i = 0; i < state->num_used_fixed; ++i) {
    PmuEventId id = state->fixed_events[i];
    DEBUG_ASSERT(id != 0);
    unsigned hw_num = state->fixed_hw_map[i];
    DEBUG_ASSERT(hw_num < perfmon_num_fixed_counters);
    uint64_t count = read_msr(IA32_FIXED_CTR0 + hw_num);
    if (count >= state->fixed_initial_value[i]) {
      count -= state->fixed_initial_value[i];
    } else {
      // The max counter value is generally not 64 bits.
      count += (perfmon_max_fixed_counter_value - state->fixed_initial_value[i] + 1);
    }
    next = arch_perfmon_write_count_record(next, id, count);
  }
  // Misc events are currently all non-cpu-specific.
  // Just report for cpu 0. See pmi_interrupt_handler.
  if (cpu == 0) {
    for (unsigned i = 0; i < state->num_used_misc; ++i) {
      PmuEventId id = state->misc_events[i];
      ReadMiscResult typed_value = read_misc_event(state, id);
      switch (typed_value.type) {
        case perfmon::kRecordTypeCount:
          next = arch_perfmon_write_count_record(next, id, typed_value.value);
          break;
        case perfmon::kRecordTypeValue:
          next = arch_perfmon_write_value_record(next, id, typed_value.value);
          break;
        default:
          __UNREACHABLE;
      }
    }
  }

  data->buffer_next = next;
}

static void x86_perfmon_finalize_buffer(PerfmonState* state, cpu_num_t cpu) {
  LTRACEF("Collecting last data for cpu %u\n", cpu);

  PerfmonCpuData* data = &state->cpu_data[cpu];
  perfmon::BufferHeader* hdr = data->buffer_start;

  // KISS. There may be enough space to write some of what we want to write
  // here, but don't try. Just use the same simple check that
  // |pmi_interrupt_handler()| does.
  size_t space_needed = get_max_space_needed_for_all_records(state);
  if (reinterpret_cast<char*>(data->buffer_next) + space_needed > data->buffer_end) {
    hdr->flags |= perfmon::BufferHeader::kBufferFlagFull;
    LTRACEF("Buffer overflow on cpu %u\n", cpu);
  } else {
    x86_perfmon_write_last_records(state, cpu);
  }

  hdr->capture_end =
      reinterpret_cast<char*>(data->buffer_next) - reinterpret_cast<char*>(data->buffer_start);
}

static void x86_perfmon_stop_cpu_task(void* raw_context) {
  // Disable all counters ASAP.
  disable_counters();
  apic_pmi_mask();

  DEBUG_ASSERT(arch_ints_disabled());
  DEBUG_ASSERT(!atomic_load(&perfmon_active));
  DEBUG_ASSERT(raw_context);

  auto state = reinterpret_cast<PerfmonState*>(raw_context);
  auto cpu = arch_curr_cpu_num();
  auto data = &state->cpu_data[cpu];

  // Retrieve final event values and write into the trace buffer.

  if (data->buffer_start) {
    x86_perfmon_finalize_buffer(state, cpu);
  }

  x86_perfmon_clear_overflow_indicators();
}

void arch_perfmon_stop_locked() TA_REQ(PerfmonLock::Get()) {
  if (!perfmon_supported) {
    // Nothing to do.
    return;
  }
  if (!perfmon_state) {
    // Nothing to do.
    return;
  }
  if (!atomic_load(&perfmon_active)) {
    // Nothing to do.
    return;
  }

  TRACEF("Disabling perfmon\n");

  // Do this before anything else so that any PMI interrupts from this point
  // on won't try to access potentially unmapped memory.
  atomic_store(&perfmon_active, false);

  // TODO(dje): Check clobbering of values - user should be able to do
  // multiple stops and still read register values.

  auto state = perfmon_state.get();
  mp_sync_exec(MP_IPI_TARGET_ALL, 0, x86_perfmon_stop_cpu_task, state);

  // x86_perfmon_start currently maps the buffers in, so we unmap them here.
  // Make sure to do this after we've turned everything off so that we
  // don't get another PMI after this.
  x86_perfmon_unmap_buffers_locked(state);
}

// Stop collecting data.
void arch_perfmon_stop() {
  Guard<Mutex> guard(PerfmonLock::Get());
  arch_perfmon_stop_locked();
}

// Worker for x86_perfmon_fini to be executed on all cpus.
// This is invoked via mp_sync_exec which thread safety analysis cannot follow.
static void x86_perfmon_reset_task(void* raw_context) {
  DEBUG_ASSERT(arch_ints_disabled());
  DEBUG_ASSERT(!atomic_load(&perfmon_active));
  DEBUG_ASSERT(!raw_context);

  disable_counters();
  apic_pmi_mask();
  x86_perfmon_clear_overflow_indicators();

  write_msr(IA32_DEBUGCTL, 0);

  for (unsigned i = 0; i < perfmon_num_programmable_counters; ++i) {
    write_msr(IA32_PERFEVTSEL_FIRST + i, 0);
    write_msr(IA32_PMC_FIRST + i, 0);
  }

  write_msr(IA32_FIXED_CTR_CTRL, 0);
  for (unsigned i = 0; i < perfmon_num_fixed_counters; ++i) {
    write_msr(IA32_FIXED_CTR0 + i, 0);
  }
}

// Finish data collection, reset h/w back to initial state and undo
// everything x86_perfmon_init did.
void arch_perfmon_fini() {
  Guard<Mutex> guard(PerfmonLock::Get());

  if (!perfmon_supported) {
    // Nothing to do.
    return;
  }

  if (atomic_load(&perfmon_active)) {
    arch_perfmon_stop_locked();
    DEBUG_ASSERT(!atomic_load(&perfmon_active));
  }

  mp_sync_exec(MP_IPI_TARGET_ALL, 0, x86_perfmon_reset_task, nullptr);

  perfmon_state.reset();
}

// Interrupt handling.

// Write out a |perfmon::LastBranchRecord| record.
static perfmon::RecordHeader* x86_perfmon_write_last_branches(PerfmonState* state, uint64_t cr3,
                                                              perfmon::RecordHeader* hdr,
                                                              PmuEventId id) {
  auto rec = reinterpret_cast<perfmon::LastBranchRecord*>(hdr);
  auto num_entries = perfmon_lbr_stack_size;
  static_assert(
      perfmon::LastBranchRecord::kMaxNumLastBranch == countof(perfmon::LastBranchRecord::branches),
      "");
  DEBUG_ASSERT(num_entries > 0 && num_entries <= perfmon::LastBranchRecord::kMaxNumLastBranch);
  arch_perfmon_write_header(&rec->header, perfmon::kRecordTypeLastBranch, id);
  rec->num_branches = num_entries;
  rec->aspace = cr3;

  auto branches = &rec->branches[0];
  unsigned tos =
      ((read_msr(SKL_LAST_BRANCH_TOS) & IA32_LBR_TOS_TOS_MASK) >> IA32_LBR_TOS_TOS_SHIFT);
  for (unsigned i = 0; i < num_entries; ++i) {
    unsigned msr_offset = (tos - i) % num_entries;
    branches[i].from = read_msr(SKL_LAST_BRANCH_FROM_0 + msr_offset);
    branches[i].to = read_msr(SKL_LAST_BRANCH_TO_0 + msr_offset);
    uint64_t info = read_msr(SKL_LAST_BRANCH_INFO_0 + msr_offset);
    // Only write these bits out.
    info &= (IA32_LBR_INFO_CYCLE_COUNT_MASK | IA32_LBR_INFO_MISPRED_MASK);
    branches[i].info = info;
  }

  // Get a pointer to the end of this record. Since this record is
  // variable length it's more complicated than just "rec + 1".
  auto next = reinterpret_cast<perfmon::RecordHeader*>(reinterpret_cast<char*>(rec) +
                                                       perfmon::LastBranchRecordSize(rec));
  LTRACEF("LBR record: num branches %u, @%p, next @%p\n", num_entries, hdr, next);
  return next;
}

// Helper function so that there is only one place where we enable/disable
// interrupts (our caller).
// Returns true if success, false if buffer is full.

static bool pmi_interrupt_handler(x86_iframe_t* frame, PerfmonState* state) {
  cpu_num_t cpu = arch_curr_cpu_num();
  auto data = &state->cpu_data[cpu];

  // On x86 zx_ticks_get uses rdtsc.
  zx_time_t now = _rdtsc();
  LTRACEF("cpu %u: now %" PRIi64 ", sp %p\n", cpu, now, __GET_FRAME());

  // Rather than continually checking if we have enough space, just
  // conservatively check for the maximum amount we'll need.
  size_t space_needed = get_max_space_needed_for_all_records(state);
  if (reinterpret_cast<char*>(data->buffer_next) + space_needed > data->buffer_end) {
    TRACEF("cpu %u: @%" PRIi64 " pmi buffer full\n", cpu, now);
    data->buffer_start->flags |= perfmon::BufferHeader::kBufferFlagFull;
    return false;
  }

  const uint64_t status = read_msr(IA32_PERF_GLOBAL_STATUS);
  uint64_t bits_to_clear = 0;
  uint64_t cr3 = x86_get_cr3();

  LTRACEF("cpu %u: status 0x%" PRIx64 "\n", cpu, status);

  if (status & perfmon_counter_status_bits) {
#if TRY_FREEZE_ON_PMI
    if (!(status & IA32_PERF_GLOBAL_STATUS_CTR_FRZ_MASK))
      LTRACEF("Eh? status.CTR_FRZ not set\n");
#else
    if (status & IA32_PERF_GLOBAL_STATUS_CTR_FRZ_MASK)
      LTRACEF("Eh? status.CTR_FRZ is set\n");
#endif

    auto next = data->buffer_next;
    bool saw_timebase = false;
    // We can't record every event that requested LBR data.
    // It is unspecified which one we pick.
    PmuEventId lbr_id = perfmon::kEventIdNone;

    next = arch_perfmon_write_time_record(next, perfmon::kEventIdNone, now);

    // Note: We don't write "value" records here instead prefering the
    // smaller "tick" record. If the user is tallying the counts the user
    // is required to recognize this and apply the tick rate.
    // TODO(dje): Precompute mask to detect whether the interrupt is for
    // the timebase counter, and then combine the loops.

    for (unsigned i = 0; i < state->num_used_programmable; ++i) {
      if (!(status & IA32_PERF_GLOBAL_STATUS_PMC_OVF_MASK(i)))
        continue;
      PmuEventId id = state->programmable_events[i];
      // Counters using a separate timebase are handled below.
      // We shouldn't get an interrupt on a counter using a timebase.
      // TODO(dje): The counter could still overflow. Later.
      if (id == state->timebase_event) {
        saw_timebase = true;
      } else if (state->programmable_flags[i] & perfmon::kPmuConfigFlagUsesTimebase) {
        continue;
      }
      if (state->programmable_flags[i] & perfmon::kPmuConfigFlagPc) {
        next = arch_perfmon_write_pc_record(next, id, cr3, frame->ip);
      } else {
        next = arch_perfmon_write_tick_record(next, id);
      }
      if (state->programmable_flags[i] & perfmon::kPmuConfigFlagLastBranch) {
        lbr_id = id;
      }
      LTRACEF("cpu %u: resetting PMC %u to 0x%" PRIx64 "\n", cpu, i,
              state->programmable_initial_value[i]);
      write_msr(IA32_PMC_FIRST + i, state->programmable_initial_value[i]);
    }

    for (unsigned i = 0; i < state->num_used_fixed; ++i) {
      unsigned hw_num = state->fixed_hw_map[i];
      DEBUG_ASSERT(hw_num < perfmon_num_fixed_counters);
      if (!(status & IA32_PERF_GLOBAL_STATUS_FIXED_OVF_MASK(hw_num)))
        continue;
      PmuEventId id = state->fixed_events[i];
      // Counters using a separate timebase are handled below.
      // We shouldn't get an interrupt on a counter using a timebase.
      // TODO(dje): The counter could still overflow. Later.
      if (id == state->timebase_event) {
        saw_timebase = true;
      } else if (state->fixed_flags[i] & perfmon::kPmuConfigFlagUsesTimebase) {
        continue;
      }
      if (state->fixed_flags[i] & perfmon::kPmuConfigFlagPc) {
        next = arch_perfmon_write_pc_record(next, id, cr3, frame->ip);
      } else {
        next = arch_perfmon_write_tick_record(next, id);
      }
      if (state->fixed_flags[i] & perfmon::kPmuConfigFlagLastBranch) {
        lbr_id = id;
      }
      LTRACEF("cpu %u: resetting FIXED %u to 0x%" PRIx64 "\n", cpu, hw_num,
              state->fixed_initial_value[i]);
      write_msr(IA32_FIXED_CTR0 + hw_num, state->fixed_initial_value[i]);
    }

    bits_to_clear |= perfmon_counter_status_bits;

    // Now handle events that have kPmuConfigFlagTimebase0 set.
    if (saw_timebase) {
      for (unsigned i = 0; i < state->num_used_programmable; ++i) {
        if (!(state->programmable_flags[i] & perfmon::kPmuConfigFlagUsesTimebase))
          continue;
        PmuEventId id = state->programmable_events[i];
        uint64_t count = read_msr(IA32_PMC_FIRST + i);
        next = arch_perfmon_write_count_record(next, id, count);
        // We could leave the counter alone, but it could overflow.
        // Instead reduce the risk and just always reset to zero.
        LTRACEF("cpu %u: resetting PMC %u to 0x%" PRIx64 "\n", cpu, i,
                state->programmable_initial_value[i]);
        write_msr(IA32_PMC_FIRST + i, state->programmable_initial_value[i]);
      }
      for (unsigned i = 0; i < state->num_used_fixed; ++i) {
        if (!(state->fixed_flags[i] & perfmon::kPmuConfigFlagUsesTimebase))
          continue;
        PmuEventId id = state->fixed_events[i];
        unsigned hw_num = state->fixed_hw_map[i];
        DEBUG_ASSERT(hw_num < perfmon_num_fixed_counters);
        uint64_t count = read_msr(IA32_FIXED_CTR0 + hw_num);
        next = arch_perfmon_write_count_record(next, id, count);
        // We could leave the counter alone, but it could overflow.
        // Instead reduce the risk and just always reset to zero.
        LTRACEF("cpu %u: resetting FIXED %u to 0x%" PRIx64 "\n", cpu, hw_num,
                state->fixed_initial_value[i]);
        write_msr(IA32_FIXED_CTR0 + hw_num, state->fixed_initial_value[i]);
      }
      // Misc events are currently all non-cpu-specific. We have a
      // timebase driving their collection, but useful timebases
      // are triggered on each cpu. One thing we'd like to avoid is
      // contention for the cache line containing these counters.
      // For now, only collect data when we're running on cpu 0.
      // This is not ideal, it could be mostly idle. OTOH, some
      // interrupts are currently only serviced on cpu 0 so that
      // ameliorates the problem somewhat.
      if (cpu == 0) {
        for (unsigned i = 0; i < state->num_used_misc; ++i) {
          if (!(state->misc_flags[i] & perfmon::kPmuConfigFlagUsesTimebase)) {
            // While a timebase is required for all current misc
            // counters, we don't assume this here.
            continue;
          }
          PmuEventId id = state->misc_events[i];
          ReadMiscResult typed_value = read_misc_event(state, id);
          switch (typed_value.type) {
            case perfmon::kRecordTypeCount:
              next = arch_perfmon_write_count_record(next, id, typed_value.value);
              break;
            case perfmon::kRecordTypeValue:
              next = arch_perfmon_write_value_record(next, id, typed_value.value);
              break;
            default:
              __UNREACHABLE;
          }
        }
      }
    }

    if (lbr_id != perfmon::kEventIdNone) {
      next = x86_perfmon_write_last_branches(state, cr3, next, lbr_id);
    }

    data->buffer_next = next;
  }

  // We shouldn't be seeing these set (at least not yet).
  if (status & IA32_PERF_GLOBAL_STATUS_TRACE_TOPA_PMI_MASK)
    LTRACEF("WARNING: GLOBAL_STATUS_TRACE_TOPA_PMI set\n");
  if (status & IA32_PERF_GLOBAL_STATUS_LBR_FRZ_MASK)
    LTRACEF("WARNING: GLOBAL_STATUS_LBR_FRZ set\n");
  if (status & IA32_PERF_GLOBAL_STATUS_DS_BUFFER_OVF_MASK)
    LTRACEF("WARNING: GLOBAL_STATUS_DS_BUFFER_OVF set\n");
  // TODO(dje): IA32_PERF_GLOBAL_STATUS_ASCI_MASK ???

  // Note IA32_PERF_GLOBAL_STATUS_CTR_FRZ_MASK is readonly.
  bits_to_clear |=
      (IA32_PERF_GLOBAL_STATUS_UNCORE_OVF_MASK | IA32_PERF_GLOBAL_STATUS_COND_CHGD_MASK);

  // TODO(dje): No need to accumulate bits to clear if we're going to clear
  // everything that's set anyway. Kept as is during development.
  bits_to_clear |= status;

  LTRACEF("cpu %u: clearing status bits 0x%" PRIx64 "\n", cpu, bits_to_clear);
  write_msr(IA32_PERF_GLOBAL_STATUS_RESET, bits_to_clear);

  // TODO(dje): Always do this test for now. Later conditionally include
  // via some debugging macro.
  uint64_t end_status = read_msr(IA32_PERF_GLOBAL_STATUS);
  if (end_status != 0)
    TRACEF("WARNING: cpu %u: end status 0x%" PRIx64 "\n", cpu, end_status);

  return true;
}

void apic_pmi_interrupt_handler(x86_iframe_t* frame) TA_REQ(PerfmonLock::Get()) {
  if (!atomic_load(&perfmon_active)) {
    apic_issue_eoi();
    return;
  }

#if TRY_FREEZE_ON_PMI
  // Note: We're using perfmon v4 "streamlined" processing here.
  // See Intel vol3 table 17-3 "Legacy and Streamlined Operation with
  // Freeze_Perfmon_On_PMI = 1, Counter Overflowed".
#else
  // Turn all counters off as soon as possible so that the counters that
  // haven't overflowed yet stop counting while we're working.
  // TODO(dje): Is this necessary with CTR_FRZ?
  // Otherwise once we reset the counter that overflowed the other counters
  // will resume counting, and if we don't reset them too then CTR_FRZ
  // remains set and we'll get no more PMIs.
  disable_counters();
#endif

  DEBUG_ASSERT(arch_ints_disabled());

  CPU_STATS_INC(perf_ints);

  auto state = perfmon_state.get();

#if 0
    // TODO(dje): We may want this anyway. If we want to be able to handle
    // page faults inside this handler we'll need to turn interrupts back
    // on. At the moment we can't do this as we don't handle recursive PMIs.
    arch_set_blocking_disallowed(false);
    arch_enable_ints();
#endif

  bool success = pmi_interrupt_handler(frame, state);

#if 0
    arch_disable_ints();
    arch_set_blocking_disallowed(true);
#endif

  // This is done here instead of in the caller so that we have full control
  // of when counting is restored.
  apic_issue_eoi();

  // If buffer is full leave everything turned off.
  if (!success) {
#if TRY_FREEZE_ON_PMI
    disable_counters();
#else
    // Don't restore GLOBAL_CTRL, leave everything turned off.
#endif
  } else {
    // The docs suggest this is only necessary for earlier chips
    // (e.g., not Skylake). Intel vol3 section 10.5.1 "Local Vector Table".
    // However, this is needed for at least Skylake too (at least when
    // Freeze-On-PMI is off).
    apic_pmi_unmask();

#if !TRY_FREEZE_ON_PMI
    // This is the last thing we do: Once we do this the counters
    // will start counting again.
    enable_counters(state);
#endif
  }
}
