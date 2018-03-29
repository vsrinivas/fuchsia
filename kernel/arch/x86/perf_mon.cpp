// Copyright 2017 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

// A note on the distribution of code between us and the userspace driver:
// The default location for code is the userspace driver. Reasons for
// putting code here are: implementation requirement (need ring zero to write
// MSRs), stability, and performance. The device driver should do as much
// error checking as possible before calling us.
// Note that we do a lot of verification of the input configuration:
// We don't want to be compromised if the userspace driver gets compromised.

// A note on terminology: "events" vs "counters": A "counter" is an
// "event", but some events are not counters. Internally, we use the
// term "counter" when we know the event is a counter.

// TODO(dje): wip
// The thought is to use resources (as in ResourceDispatcher), at which point
// this will all get rewritten. Until such time, the goal here is KISS.
// This file contains the lower part of Intel Performance Monitor support that
// must be done in the kernel (so that we can read/write msrs).
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

#include <arch/arch_ops.h>
#include <arch/mmu.h>
#include <arch/x86.h>
#include <arch/x86/apic.h>
#include <arch/x86/feature.h>
#include <arch/x86/mmu.h>
#include <arch/x86/perf_mon.h>
#include <assert.h>
#include <dev/pci_common.h>
#include <err.h>
#include <fbl/alloc_checker.h>
#include <fbl/macros.h>
#include <fbl/ref_ptr.h>
#include <fbl/unique_ptr.h>
#include <kernel/align.h>
#include <kernel/mp.h>
#include <kernel/mutex.h>
#include <kernel/stats.h>
#include <kernel/thread.h>
#include <platform.h>
#include <vm/vm.h>
#include <vm/vm_address_region.h>
#include <vm/vm_aspace.h>
#include <vm/vm_object_physical.h>
#include <lib/ktrace.h>
#include <lib/pci/pio.h>
#include <zircon/device/cpu-trace/cpu-perf.h>
#include <zircon/device/cpu-trace/intel-pm.h>
#include <zircon/ktrace.h>
#include <zircon/mtrace.h>
#include <zircon/thread_annotations.h>
#include <zxcpp/new.h>
#include <pow2.h>
#include <string.h>
#include <trace.h>

#define LOCAL_TRACE 0

// There's only a few misc events, and they're non-homogenous,
// so handle them directly.
typedef enum {
#define DEF_MISC_SKL_EVENT(symbol, id, offset, size, flags, name, description) \
    symbol ## _ID = CPUPERF_MAKE_EVENT_ID(CPUPERF_UNIT_MISC, id),
#include <zircon/device/cpu-trace/skylake-misc-events.inc>
} misc_event_id_t;

// h/w address of misc events.
typedef enum {
#define DEF_MISC_SKL_EVENT(symbol, id, offset, size, flags, name, description) \
    symbol ## _OFFSET = offset,
#include <zircon/device/cpu-trace/skylake-misc-events.inc>
} misc_event_offset_t;

// TODO(dje): Freeze-on-PMI doesn't work in skylake.
// This is here for experimentation purposes.
#define TRY_FREEZE_ON_PMI 0

// At a minimum we require Performance Monitoring version 4.
// KISS: Skylake supports version 4.
#define MINIMUM_PERFMON_VERSION 4

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
#define IA32_PERF_GLOBAL_STATUS_RESET 0x390 // Yes, same as OVF_CTRL.
#define IA32_PERF_GLOBAL_STATUS_SET 0x391
#define IA32_PERF_GLOBAL_INUSE 0x392

#define IA32_DEBUGCTL 0x1d9

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
#define UNC_IMC_STATS_BEGIN 0x5040 // MISC_MEM_GT_REQUESTS
// Offset from BAR of the last byte we need to map.
#define UNC_IMC_STATS_END   0x5983 // MISC_PKG_GT_TEMP

// Verify all values are within [BEGIN,END].
#define DEF_MISC_SKL_EVENT(symbol, id, offset, size, flags, name, description) \
    && (offset >= UNC_IMC_STATS_BEGIN && (offset + size/8) <= UNC_IMC_STATS_END + 1)
static_assert(1
#include <zircon/device/cpu-trace/skylake-misc-events.inc>
    , "");

// These aren't constexpr as we iterate to fill in values for each counter.
static uint64_t kGlobalCtrlWritableBits;
static uint64_t kFixedCounterCtrlWritableBits;

static constexpr size_t kMaxEventRecordSize = sizeof(cpuperf_pc_record_t);

// Commented out values represent currently unsupported features.
// They remain present for documentation purposes.
static constexpr uint64_t kDebugCtrlWritableBits =
    (/*IA32_DEBUGCTL_LBR_MASK |*/
     /*IA32_DEBUGCTL_BTF_MASK |*/
     /*IA32_DEBUGCTL_TR_MASK |*/
     /*IA32_DEBUGCTL_BTS_MASK |*/
     /*IA32_DEBUGCTL_BTINT_MASK |*/
     /*IA32_DEBUGCTL_BTS_OFF_OS_MASK |*/
     /*IA32_DEBUGCTL_BTS_OFF_USR_MASK |*/
     /*IA32_DEBUGCTL_FREEZE_LBRS_ON_PMI_MASK |*/
#if TRY_FREEZE_ON_PMI
     IA32_DEBUGCTL_FREEZE_PERFMON_ON_PMI_MASK |
#endif
     /*IA32_DEBUGCTL_FREEZE_WHILE_SMM_EN_MASK |*/
     /*IA32_DEBUGCTL_RTM_MASK |*/
     0);
static constexpr uint64_t kEventSelectWritableBits =
    (IA32_PERFEVTSEL_EVENT_SELECT_MASK |
     IA32_PERFEVTSEL_UMASK_MASK |
     IA32_PERFEVTSEL_USR_MASK |
     IA32_PERFEVTSEL_OS_MASK |
     IA32_PERFEVTSEL_E_MASK |
     IA32_PERFEVTSEL_PC_MASK |
     IA32_PERFEVTSEL_INT_MASK |
     IA32_PERFEVTSEL_ANY_MASK |
     IA32_PERFEVTSEL_EN_MASK |
     IA32_PERFEVTSEL_INV_MASK |
     IA32_PERFEVTSEL_CMASK_MASK);

static bool supports_perfmon = false;

static uint32_t perfmon_version = 0;
static uint32_t perfmon_num_programmable_counters = 0;
static uint32_t perfmon_programmable_counter_width = 0;
static uint32_t perfmon_num_fixed_counters = 0;
static uint32_t perfmon_fixed_counter_width = 0;
static uint32_t perfmon_unsupported_events = 0;
static uint32_t perfmon_capabilities = 0;

// Maximum counter values, derived from their width.
static uint64_t perfmon_max_fixed_counter_value = 0;
static uint64_t perfmon_max_programmable_counter_value = 0;

// Counter bits in GLOBAL_STATUS to check on each interrupt.
static uint64_t perfmon_counter_status_bits = 0;

// BAR (base address register) of Intel MCHBAR performance
// registers. These registers are accessible via mmio.
static uint32_t perfmon_mchbar_bar = 0;

// The number of "miscellaneous" events we can handle at once.
static uint32_t perfmon_num_misc_events = 0;

struct PerfmonCpuData {
    // The trace buffer, passed in from userspace.
    fbl::RefPtr<VmObject> buffer_vmo;
    size_t buffer_size = 0;

    // The trace buffer when mapped into kernel space.
    // This is only done while the trace is running.
    fbl::RefPtr<VmMapping> buffer_mapping;
    cpuperf_buffer_header_t* buffer_start = 0;
    void* buffer_end = 0;

    // The next record to fill.
    cpuperf_record_header_t* buffer_next = nullptr;
} __CPU_ALIGN;

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

struct PerfmonState {
    static zx_status_t Create(unsigned n_cpus, fbl::unique_ptr<PerfmonState>* out_state);
    explicit PerfmonState(unsigned n_cpus);
    ~PerfmonState();

    // IA32_PERF_GLOBAL_CTRL
    uint64_t global_ctrl = 0;

    // IA32_FIXED_CTR_CTRL
    uint64_t fixed_ctrl = 0;

    // IA32_DEBUGCTL
    uint64_t debug_ctrl = 0;

    // True if MCHBAR perf regs need to be mapped in.
    bool need_mchbar = false;

    // See intel-pm.h:zx_x86_ipm_config_t.
    cpuperf_event_id_t timebase_id = CPUPERF_EVENT_ID_NONE;

    // The number of each kind of event in use, so we don't have to iterate
    // over the entire arrays.
    unsigned num_used_fixed = 0;
    unsigned num_used_programmable = 0;
    unsigned num_used_misc = 0;

    // Number of entries in |cpu_data|.
    const unsigned num_cpus;

    // An array with one entry for each cpu.
    // TODO(dje): Ideally this would be something like
    // fbl::unique_ptr<PerfmonCpuData[]> cpu_data;
    // but that will need to wait for a "new" that handles aligned allocs.
    PerfmonCpuData* cpu_data = nullptr;

    MemoryControllerHubData mchbar_data;

    // |fixed_hw_map[i]| is the h/w fixed counter number.
    // This is used to only look at fixed counters that are used.
    unsigned fixed_hw_map[IPM_MAX_FIXED_COUNTERS] = {};

    // The counters are reset to this at the start.
    // And again for those that are reset on overflow.
    uint64_t fixed_initial_value[IPM_MAX_FIXED_COUNTERS] = {};
    uint64_t programmable_initial_value[IPM_MAX_PROGRAMMABLE_COUNTERS] = {};

    // Flags for each event/counter, IPM_CONFIG_FLAG_*.
    uint32_t fixed_flags[IPM_MAX_FIXED_COUNTERS] = {};
    uint32_t programmable_flags[IPM_MAX_PROGRAMMABLE_COUNTERS] = {};
    uint32_t misc_flags[IPM_MAX_MISC_EVENTS] = {};

    // The ids for each of the in-use events, or zero if not used.
    // These are passed in from the driver and then written to the buffer,
    // but otherwise have no meaning to us.
    // All in-use entries appear consecutively.
    cpuperf_event_id_t fixed_ids[IPM_MAX_FIXED_COUNTERS] = {};
    cpuperf_event_id_t programmable_ids[IPM_MAX_PROGRAMMABLE_COUNTERS] = {};
    cpuperf_event_id_t misc_ids[IPM_MAX_MISC_EVENTS] = {};

    // IA32_PERFEVTSEL_*
    uint64_t events[IPM_MAX_PROGRAMMABLE_COUNTERS] = {};
};

static fbl::Mutex perfmon_lock;

static fbl::unique_ptr<PerfmonState> perfmon_state TA_GUARDED(perfmon_lock);

// This is accessed atomically as it is also accessed by the PMI handler.
static int perfmon_active = false;

zx_status_t PerfmonState::Create(unsigned n_cpus, fbl::unique_ptr<PerfmonState>* out_state) {
    fbl::AllocChecker ac;
    auto state = fbl::unique_ptr<PerfmonState>(new (&ac) PerfmonState(n_cpus));
    if (!ac.check())
        return ZX_ERR_NO_MEMORY;

    size_t space_needed = sizeof(PerfmonCpuData) * n_cpus;
    auto cpu_data = reinterpret_cast<PerfmonCpuData*>(
        memalign(alignof(PerfmonCpuData), space_needed));
    if (!cpu_data)
        return ZX_ERR_NO_MEMORY;

    for (unsigned cpu = 0; cpu < n_cpus; ++cpu) {
        new (&cpu_data[cpu]) PerfmonCpuData();
    }

    state->cpu_data = cpu_data;
    *out_state = fbl::move(state);
    return ZX_OK;
}

PerfmonState::PerfmonState(unsigned n_cpus)
        : num_cpus(n_cpus) { }

PerfmonState::~PerfmonState() {
    DEBUG_ASSERT(!atomic_load(&perfmon_active));
    if (cpu_data) {
        for (unsigned cpu = 0; cpu < num_cpus; ++cpu) {
            auto data = &cpu_data[cpu];
            data->~PerfmonCpuData();
        }
        free(cpu_data);
    }
}

static bool x86_perfmon_have_mchbar_data() {
    uint32_t vendor_id, device_id;

    auto status = Pci::PioCfgRead(0, 0, 0,
                                  PCI_CONFIG_VENDOR_ID,
                                  &vendor_id, 16);
    if (status != ZX_OK)
        return false;
    if (vendor_id != INTEL_MCHBAR_PCI_VENDOR_ID)
        return false;
    status = Pci::PioCfgRead(0, 0, 0,
                             PCI_CONFIG_DEVICE_ID,
                             &device_id, 16);
    if (status != ZX_OK)
        return false;
    for (auto supported_device_id : supported_mem_device_ids) {
        if (supported_device_id == device_id)
            return true;
    }

    TRACEF("perfmon: unsupported pci device: 0x%x.0x%x\n",
           vendor_id, device_id);
    return false;
}

static void x86_perfmon_init_mchbar() {
    uint32_t bar;
    auto status = Pci::PioCfgRead(0, 0, 0,
                                  INTEL_MCHBAR_PCI_CONFIG_OFFSET,
                                  &bar, 32);
    if (status == ZX_OK) {
        LTRACEF("perfmon: mchbar: 0x%x\n", bar);
        // TODO(dje): The lower four bits contain useful data, but punt for now.
        // See PCI spec 6.2.5.1.
        perfmon_mchbar_bar = bar & ~15u;
        perfmon_num_misc_events =
            countof(static_cast<zx_x86_ipm_config_t*>(nullptr)->misc_ids);
    } else {
        TRACEF("perfmon: error %d reading mchbar\n", status);
    }
}

void x86_perfmon_init(void)
{
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
    if (perfmon_programmable_counter_width < 16 ||
        perfmon_programmable_counter_width > 64) {
        TRACEF("perfmon: unexpected programmable counter width %u in cpuid.0AH\n",
               perfmon_programmable_counter_width);
        return;
    }
    perfmon_max_programmable_counter_value = ~0ul;
    if (perfmon_programmable_counter_width < 64) {
        perfmon_max_programmable_counter_value =
            (1ul << perfmon_programmable_counter_width) - 1;
    }

    unsigned ebx_length = (leaf.a >> 24) & 0xff;
    if (ebx_length > 7) {
        TRACEF("perfmon: unexpected value %u in cpuid.0AH.EAH[31..24]\n",
               ebx_length);
        return;
    }
    perfmon_unsupported_events = leaf.b & ((1u << ebx_length) - 1);

    perfmon_num_fixed_counters = leaf.d & 0x1f;
    if (perfmon_num_fixed_counters > IPM_MAX_FIXED_COUNTERS) {
        TRACEF("perfmon: unexpected num fixed counters %u in cpuid.0AH\n",
               perfmon_num_fixed_counters);
        return;
    }
    perfmon_fixed_counter_width = (leaf.d >> 5) & 0xff;
    // The <16 test is just something simple to ensure it's usable.
    if (perfmon_fixed_counter_width < 16 ||
        perfmon_fixed_counter_width > 64) {
        TRACEF("perfmon: unexpected fixed counter width %u in cpuid.0AH\n",
               perfmon_fixed_counter_width);
        return;
    }
    perfmon_max_fixed_counter_value = ~0ul;
    if (perfmon_fixed_counter_width < 64) {
        perfmon_max_fixed_counter_value =
            (1ul << perfmon_fixed_counter_width) - 1;
    }

    supports_perfmon = perfmon_version >= MINIMUM_PERFMON_VERSION;

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
}

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
static unsigned x86_perfmon_lookup_fixed_counter(cpuperf_event_id_t id) {
    if (CPUPERF_EVENT_ID_UNIT(id) != CPUPERF_UNIT_FIXED)
        return IPM_MAX_FIXED_COUNTERS;
    switch (CPUPERF_EVENT_ID_EVENT(id)) {
#define DEF_FIXED_EVENT(symbol, id, regnum, flags, name, description) \
    case id: return regnum;
#include <zircon/device/cpu-trace/intel-pm-events.inc>
    default: return IPM_MAX_FIXED_COUNTERS;
    }
}

static void x86_perfmon_write_header(cpuperf_record_header_t* hdr,
                                     cpuperf_record_type_t type,
                                     cpuperf_event_id_t event) {
    hdr->type = type;
    hdr->reserved_flags = 0;
    hdr->event = event;
}

static cpuperf_record_header_t* x86_perfmon_write_time_record(
        cpuperf_record_header_t* hdr,
        cpuperf_event_id_t event, zx_time_t time) {
    auto rec = reinterpret_cast<cpuperf_time_record_t*>(hdr);
    x86_perfmon_write_header(&rec->header, CPUPERF_RECORD_TIME, event);
    rec->time = time;
    ++rec;
    return reinterpret_cast<cpuperf_record_header_t*>(rec);
}

static cpuperf_record_header_t* x86_perfmon_write_tick_record(
        cpuperf_record_header_t* hdr,
        cpuperf_event_id_t event) {
    auto rec = reinterpret_cast<cpuperf_tick_record_t*>(hdr);
    x86_perfmon_write_header(&rec->header, CPUPERF_RECORD_TICK, event);
    ++rec;
    return reinterpret_cast<cpuperf_record_header_t*>(rec);
}

static cpuperf_record_header_t* x86_perfmon_write_count_record(
        cpuperf_record_header_t* hdr,
        cpuperf_event_id_t event, uint64_t count) {
    auto rec = reinterpret_cast<cpuperf_count_record_t*>(hdr);
    x86_perfmon_write_header(&rec->header, CPUPERF_RECORD_COUNT, event);
    rec->count = count;
    ++rec;
    return reinterpret_cast<cpuperf_record_header_t*>(rec);
}

static cpuperf_record_header_t* x86_perfmon_write_value_record(
        cpuperf_record_header_t* hdr,
        cpuperf_event_id_t event, uint64_t value) {
    auto rec = reinterpret_cast<cpuperf_value_record_t*>(hdr);
    x86_perfmon_write_header(&rec->header, CPUPERF_RECORD_VALUE, event);
    rec->value = value;
    ++rec;
    return reinterpret_cast<cpuperf_record_header_t*>(rec);
}

static cpuperf_record_header_t* x86_perfmon_write_pc_record(
        cpuperf_record_header_t* hdr,
        cpuperf_event_id_t event, uint64_t cr3, uint64_t pc) {
    auto rec = reinterpret_cast<cpuperf_pc_record_t*>(hdr);
    x86_perfmon_write_header(&rec->header, CPUPERF_RECORD_PC, event);
    rec->aspace = cr3;
    rec->pc = pc;
    ++rec;
    return reinterpret_cast<cpuperf_record_header_t*>(rec);
}

zx_status_t x86_ipm_get_properties(zx_x86_ipm_properties_t* props) {
    fbl::AutoLock al(&perfmon_lock);

    if (!supports_perfmon)
        return ZX_ERR_NOT_SUPPORTED;
    props->pm_version = perfmon_version;
    props->num_fixed_events = perfmon_num_fixed_counters;
    props->num_programmable_events = perfmon_num_programmable_counters;
    props->num_misc_events = perfmon_num_misc_events;
    props->fixed_counter_width = perfmon_fixed_counter_width;
    props->programmable_counter_width = perfmon_programmable_counter_width;
    props->perf_capabilities = perfmon_capabilities;
    return ZX_OK;
}

zx_status_t x86_ipm_init() {
    fbl::AutoLock al(&perfmon_lock);

    if (!supports_perfmon)
        return ZX_ERR_NOT_SUPPORTED;
    if (atomic_load(&perfmon_active))
        return ZX_ERR_BAD_STATE;
    if (perfmon_state)
        return ZX_ERR_BAD_STATE;

    fbl::unique_ptr<PerfmonState> state;
    auto status = PerfmonState::Create(arch_max_num_cpus(), &state);
    if (status != ZX_OK)
        return status;

    perfmon_state = fbl::move(state);
    return ZX_OK;
}

zx_status_t x86_ipm_assign_buffer(uint32_t cpu, fbl::RefPtr<VmObject> vmo) {
    fbl::AutoLock al(&perfmon_lock);

    if (!supports_perfmon)
        return ZX_ERR_NOT_SUPPORTED;
    if (atomic_load(&perfmon_active))
        return ZX_ERR_BAD_STATE;
    if (!perfmon_state)
        return ZX_ERR_BAD_STATE;
    if (cpu >= perfmon_state->num_cpus)
        return ZX_ERR_INVALID_ARGS;

    // A simple safe approximation of the minimum size needed.
    size_t min_size_needed = sizeof(cpuperf_buffer_header_t);
    min_size_needed += sizeof(cpuperf_time_record_t);
    min_size_needed += CPUPERF_MAX_EVENTS * kMaxEventRecordSize;
    if (vmo->size() < min_size_needed)
        return ZX_ERR_INVALID_ARGS;

    auto data = &perfmon_state->cpu_data[cpu];
    data->buffer_vmo = vmo;
    data->buffer_size = vmo->size();
    // The buffer is mapped into kernelspace later.

    return ZX_OK;
}

static zx_status_t x86_ipm_verify_control_config(
        const zx_x86_ipm_config_t* config) {
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

static zx_status_t x86_ipm_verify_fixed_config(
        const zx_x86_ipm_config_t* config, unsigned* out_num_used) {
    bool seen_last = false;
    unsigned num_used = perfmon_num_fixed_counters;
    for (unsigned i = 0; i < perfmon_num_fixed_counters; ++i) {
        cpuperf_event_id_t id = config->fixed_ids[i];
        if (id != 0 && seen_last) {
            TRACEF("Active fixed events not front-filled\n");
            return ZX_ERR_INVALID_ARGS;
        }
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
            if (config->fixed_flags[i] & ~IPM_CONFIG_FLAG_MASK) {
                TRACEF("Unused bits set in |fixed_flags[%u]|\n", i);
                return ZX_ERR_INVALID_ARGS;
            }
            if ((config->fixed_flags[i] & IPM_CONFIG_FLAG_TIMEBASE) &&
                    config->timebase_id == CPUPERF_EVENT_ID_NONE) {
                TRACEF("Timebase requested for |fixed_flags[%u]|, but not provided\n", i);
                return ZX_ERR_INVALID_ARGS;
            }
            unsigned hw_regnum = x86_perfmon_lookup_fixed_counter(id);
            if (hw_regnum == IPM_MAX_FIXED_COUNTERS) {
                TRACEF("Invalid fixed counter id |fixed_ids[%u]|\n", i);
                return ZX_ERR_INVALID_ARGS;
            }
        }
    }

    *out_num_used = num_used;
    return ZX_OK;
}

static zx_status_t x86_ipm_verify_programmable_config(
        const zx_x86_ipm_config_t* config, unsigned* out_num_used) {
    bool seen_last = false;
    unsigned num_used = perfmon_num_programmable_counters;
    for (unsigned i = 0; i < perfmon_num_programmable_counters; ++i) {
        cpuperf_event_id_t id = config->programmable_ids[i];
        if (id != 0 && seen_last) {
            TRACEF("Active programmable events not front-filled\n");
            return ZX_ERR_INVALID_ARGS;
        }
        if (id == 0) {
            if (!seen_last)
                num_used = i;
            seen_last = true;
        }
        if (seen_last) {
            if (config->programmable_events[i] != 0) {
                TRACEF("Unused |programmable_events[%u]| not zero\n", i);
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
            if (config->programmable_events[i] & ~kEventSelectWritableBits) {
                TRACEF("Non writable bits set in |programmable_events[%u]|\n", i);
                return ZX_ERR_INVALID_ARGS;
            }
            if (config->programmable_initial_value[i] > perfmon_max_programmable_counter_value) {
                TRACEF("Initial value too large for |programmable_initial_value[%u]|\n", i);
                return ZX_ERR_INVALID_ARGS;
            }
            if (config->programmable_flags[i] & ~IPM_CONFIG_FLAG_MASK) {
                TRACEF("Unused bits set in |programmable_flags[%u]|\n", i);
                return ZX_ERR_INVALID_ARGS;
            }
            if ((config->programmable_flags[i] & IPM_CONFIG_FLAG_TIMEBASE) &&
                    config->timebase_id == CPUPERF_EVENT_ID_NONE) {
                TRACEF("Timebase requested for |programmable_flags[%u]|, but not provided\n", i);
                return ZX_ERR_INVALID_ARGS;
            }
        }
    }

    *out_num_used = num_used;
    return ZX_OK;
}

static zx_status_t x86_ipm_verify_misc_config(
        const zx_x86_ipm_config_t* config,
        unsigned* out_num_used) {
    bool seen_last = false;
    unsigned max_num_used = countof(config->misc_ids);
    unsigned num_used = max_num_used;
    for (unsigned i = 0; i < max_num_used; ++i) {
        cpuperf_event_id_t id = config->misc_ids[i];
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
                TRACEF("Unused |misc_flags[%u]| not zero\n", i);
                return ZX_ERR_INVALID_ARGS;
            }
        } else {
            if (config->misc_flags[i] & ~IPM_CONFIG_FLAG_MASK) {
                TRACEF("Unused bits set in |misc_flags[%u]|\n", i);
                return ZX_ERR_INVALID_ARGS;
            }
            // Currently we only support the MCHBAR events.
            // They cannot provide pc. We ignore the OS/USER bits.
            if (config->misc_flags[i] & IPM_CONFIG_FLAG_PC) {
                TRACEF("Invalid bits (0x%x) in |misc_flags[%u]|\n",
                       config->misc_flags[i], i);
                return ZX_ERR_INVALID_ARGS;
            }
            if ((config->misc_flags[i] & IPM_CONFIG_FLAG_TIMEBASE) &&
                    config->timebase_id == CPUPERF_EVENT_ID_NONE) {
                TRACEF("Timebase requested for |misc_flags[%u]|, but not provided\n", i);
                return ZX_ERR_INVALID_ARGS;
            }
            switch (CPUPERF_EVENT_ID_EVENT(id)) {
#define DEF_MISC_SKL_EVENT(symbol, id, offset, size, flags, name, description) \
            case id: break;
#include <zircon/device/cpu-trace/skylake-misc-events.inc>
            default:
                TRACEF("Invalid misc event id |misc_ids[%u]|\n", i);
                return ZX_ERR_INVALID_ARGS;
            }
        }
    }

    *out_num_used = num_used;
    return ZX_OK;
}

static zx_status_t x86_ipm_verify_timebase_config(
        zx_x86_ipm_config_t* config,
        unsigned num_fixed, unsigned num_programmable) {
    if (config->timebase_id == CPUPERF_EVENT_ID_NONE) {
        return ZX_OK;
    }

    for (unsigned i = 0; i < num_fixed; ++i) {
        if (config->fixed_ids[i] == config->timebase_id) {
            // The PMI code is simpler if this is the case.
            config->fixed_flags[i] &= ~IPM_CONFIG_FLAG_TIMEBASE;
            return ZX_OK;
        }
    }

    for (unsigned i = 0; i < num_programmable; ++i) {
        if (config->programmable_ids[i] == config->timebase_id) {
            // The PMI code is simpler if this is the case.
            config->programmable_flags[i] &= ~IPM_CONFIG_FLAG_TIMEBASE;
            return ZX_OK;
        }
    }

    TRACEF("Timebase 0x%x requested but not present\n", config->timebase_id);
    return ZX_ERR_INVALID_ARGS;
}

static zx_status_t x86_ipm_verify_config(zx_x86_ipm_config_t* config,
                                         PerfmonState* state) {
    auto status = x86_ipm_verify_control_config(config);
    if (status != ZX_OK)
        return status;

    unsigned num_used_fixed;
    status = x86_ipm_verify_fixed_config(config, &num_used_fixed);
    if (status != ZX_OK)
        return status;
    state->num_used_fixed = num_used_fixed;

    unsigned num_used_programmable;
    status = x86_ipm_verify_programmable_config(config, &num_used_programmable);
    if (status != ZX_OK)
        return status;
    state->num_used_programmable = num_used_programmable;

    unsigned num_used_misc;
    status = x86_ipm_verify_misc_config(config, &num_used_misc);
    if (status != ZX_OK)
        return status;
    state->num_used_misc = num_used_misc;

    status = x86_ipm_verify_timebase_config(config,
                                            state->num_used_fixed,
                                            state->num_used_programmable);
    if (status != ZX_OK)
        return status;

    return ZX_OK;
}

static void x86_ipm_stage_fixed_config(const zx_x86_ipm_config_t* config,
                                       PerfmonState* state) {
    static_assert(sizeof(state->fixed_ids) ==
                  sizeof(config->fixed_ids), "");
    memcpy(state->fixed_ids, config->fixed_ids,
           sizeof(state->fixed_ids));

    static_assert(sizeof(state->fixed_initial_value) ==
                  sizeof(config->fixed_initial_value), "");
    memcpy(state->fixed_initial_value, config->fixed_initial_value,
           sizeof(state->fixed_initial_value));

    static_assert(sizeof(state->fixed_flags) ==
                  sizeof(config->fixed_flags), "");
    memcpy(state->fixed_flags, config->fixed_flags,
           sizeof(state->fixed_flags));

    for (unsigned i = 0; i < countof(state->fixed_hw_map); ++i) {
        state->fixed_hw_map[i] = x86_perfmon_lookup_fixed_counter(config->fixed_ids[i]);
    }
}

static void x86_ipm_stage_programmable_config(const zx_x86_ipm_config_t* config,
                                              PerfmonState* state) {
    static_assert(sizeof(state->programmable_ids) ==
                  sizeof(config->programmable_ids), "");
    memcpy(state->programmable_ids, config->programmable_ids,
           sizeof(state->programmable_ids));

    static_assert(sizeof(state->programmable_initial_value) ==
                  sizeof(config->programmable_initial_value), "");
    memcpy(state->programmable_initial_value, config->programmable_initial_value,
           sizeof(state->programmable_initial_value));

    static_assert(sizeof(state->programmable_flags) ==
                  sizeof(config->programmable_flags), "");
    memcpy(state->programmable_flags, config->programmable_flags,
           sizeof(state->programmable_flags));

    static_assert(sizeof(state->events) ==
                  sizeof(config->programmable_events), "");
    memcpy(state->events, config->programmable_events, sizeof(state->events));
}

static void x86_ipm_stage_misc_config(const zx_x86_ipm_config_t* config,
                                      PerfmonState* state) {
    static_assert(sizeof(state->misc_ids) ==
                  sizeof(config->misc_ids), "");
    memcpy(state->misc_ids, config->misc_ids,
           sizeof(state->misc_ids));

    static_assert(sizeof(state->misc_flags) ==
                  sizeof(config->misc_flags), "");
    memcpy(state->misc_flags, config->misc_flags,
           sizeof(state->misc_flags));

    state->need_mchbar = false;
    for (unsigned i = 0; i < state->num_used_misc; ++i) {
        // All misc events currently come from MCHBAR.
        // When needed we can add a flag to the event to denote origin.
        switch (CPUPERF_EVENT_ID_EVENT(state->misc_ids[i])) {
#define DEF_MISC_SKL_EVENT(symbol, id, offset, size, flags, name, description) \
        case id:
#include <zircon/device/cpu-trace/skylake-misc-events.inc>
            state->need_mchbar = true;
            break;
        default:
            break;
        }
    }

    // What we'd like to do here is record the current values of these
    // events, but they're not mapped in yet.
    memset(&state->mchbar_data.last_mem, 0,
           sizeof(state->mchbar_data.last_mem));
}

// Stage the configuration for later activation by START.
// One of the main goals of this function is to verify the provided config
// is ok, e.g., it won't cause us to crash.
zx_status_t x86_ipm_stage_config(zx_x86_ipm_config_t* config) {
    fbl::AutoLock al(&perfmon_lock);

    if (!supports_perfmon)
        return ZX_ERR_NOT_SUPPORTED;
    if (atomic_load(&perfmon_active))
        return ZX_ERR_BAD_STATE;
    if (!perfmon_state)
        return ZX_ERR_BAD_STATE;

    auto state = perfmon_state.get();

    // Note: The verification pass may also alter |config| to make things
    // simpler for the implementation.
    auto status = x86_ipm_verify_config(config, state);
    if (status != ZX_OK)
        return status;

    state->global_ctrl = config->global_ctrl;
    state->fixed_ctrl = config->fixed_ctrl;
    state->debug_ctrl = config->debug_ctrl;
    state->timebase_id = config->timebase_id;

    x86_ipm_stage_fixed_config(config, state);
    x86_ipm_stage_programmable_config(config, state);
    x86_ipm_stage_misc_config(config, state);

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
    // The record type to use, either CPUPERF_RECORD_COUNT or
    // CPUPERF_RECORD_VALUE.
    uint8_t type;
};

// Read the 32-bit counter from MCHBAR and return the delta
// since the last read. We do this in part because it's easier for clients
// to process and in part to catch the cases of the counter wrapping that
// we can (they're only 32 bits in h/w and are read-only).
// WARNING: This function has the side-effect of updating |*last_value|.
static uint32_t read_mc_counter32(volatile uint32_t* addr,
                                  uint32_t* last_value_addr) {
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
static uint64_t read_mc_counter64(volatile uint64_t* addr,
                                  uint64_t* last_value_addr) {
    uint64_t value = *addr;
    uint64_t last_value = *last_value_addr;
    *last_value_addr = value;
    return value - last_value;
}

// Read the 32-bit non-counter value from MCHBAR.
static uint32_t read_mc_value32(volatile uint32_t* addr) {
    return *addr;
}

static ReadMiscResult read_mc_typed_counter32(volatile uint32_t* addr,
                                              uint32_t* last_value_addr) {
    return ReadMiscResult{
        read_mc_counter32(addr, last_value_addr), CPUPERF_RECORD_COUNT};
}

static ReadMiscResult read_mc_typed_counter64(volatile uint64_t* addr,
                                              uint64_t* last_value_addr) {
    return ReadMiscResult{
        read_mc_counter64(addr, last_value_addr), CPUPERF_RECORD_COUNT};
}

static ReadMiscResult read_mc_typed_value32(volatile uint32_t* addr) {
    return ReadMiscResult{
        read_mc_value32(addr), CPUPERF_RECORD_VALUE};
}

static volatile uint32_t* get_mc_addr32(PerfmonState* state,
                                        uint32_t hw_addr) {
    return reinterpret_cast<volatile uint32_t*>(
        reinterpret_cast<volatile char*>(state->mchbar_data.stats_addr)
        + hw_addr - UNC_IMC_STATS_BEGIN);
}

static volatile uint64_t* get_mc_addr64(PerfmonState* state,
                                        uint32_t hw_addr) {
    return reinterpret_cast<volatile uint64_t*>(
        reinterpret_cast<volatile char*>(state->mchbar_data.stats_addr)
        + hw_addr - UNC_IMC_STATS_BEGIN);
}

static ReadMiscResult read_mc_bytes_read(PerfmonState* state) {
    uint32_t value = read_mc_counter32(
        get_mc_addr32(state, MISC_MEM_BYTES_READ_OFFSET),
        &state->mchbar_data.last_mem.bytes_read);
    // Return the value in bytes, easier for human readers of the
    // resulting report.
    return ReadMiscResult{value * 64ul, CPUPERF_RECORD_COUNT};
}

static ReadMiscResult read_mc_bytes_written(PerfmonState* state) {
    uint32_t value = read_mc_counter32(
        get_mc_addr32(state, MISC_MEM_BYTES_WRITTEN_OFFSET),
        &state->mchbar_data.last_mem.bytes_written);
    // Return the value in bytes, easier for human readers of the
    // resulting report.
    return ReadMiscResult{value * 64ul, CPUPERF_RECORD_COUNT};
}

static ReadMiscResult read_mc_gt_requests(PerfmonState* state) {
    return read_mc_typed_counter32(
        get_mc_addr32(state, MISC_MEM_GT_REQUESTS_OFFSET),
        &state->mchbar_data.last_mem.gt_requests);
}

static ReadMiscResult read_mc_ia_requests(PerfmonState* state) {
    return read_mc_typed_counter32(
        get_mc_addr32(state, MISC_MEM_IA_REQUESTS_OFFSET),
        &state->mchbar_data.last_mem.ia_requests);
}

static ReadMiscResult read_mc_io_requests(PerfmonState* state) {
    return read_mc_typed_counter32(
        get_mc_addr32(state, MISC_MEM_IO_REQUESTS_OFFSET),
        &state->mchbar_data.last_mem.io_requests);
}

static ReadMiscResult read_mc_all_active_core_cycles(PerfmonState* state) {
    return read_mc_typed_counter64(
        get_mc_addr64(state, MISC_PKG_ALL_ACTIVE_CORE_CYCLES_OFFSET),
        &state->mchbar_data.last_mem.all_active_core_cycles);
}

static ReadMiscResult read_mc_any_active_core_cycles(PerfmonState* state) {
    return read_mc_typed_counter64(
        get_mc_addr64(state, MISC_PKG_ANY_ACTIVE_CORE_CYCLES_OFFSET),
        &state->mchbar_data.last_mem.any_active_core_cycles);
}

static ReadMiscResult read_mc_active_gt_cycles(PerfmonState* state) {
    return read_mc_typed_counter64(
        get_mc_addr64(state, MISC_PKG_ACTIVE_GT_CYCLES_OFFSET),
        &state->mchbar_data.last_mem.active_gt_cycles);
}

static ReadMiscResult read_mc_active_ia_gt_cycles(PerfmonState* state) {
    return read_mc_typed_counter64(
        get_mc_addr64(state, MISC_PKG_ACTIVE_IA_GT_CYCLES_OFFSET),
        &state->mchbar_data.last_mem.active_ia_gt_cycles);
}

static ReadMiscResult read_mc_active_gt_slice_cycles(PerfmonState* state) {
    return read_mc_typed_counter64(
        get_mc_addr64(state, MISC_PKG_ACTIVE_GT_SLICE_CYCLES_OFFSET),
        &state->mchbar_data.last_mem.active_gt_slice_cycles);
}

static ReadMiscResult read_mc_active_gt_engine_cycles(PerfmonState* state) {
    return read_mc_typed_counter64(
        get_mc_addr64(state, MISC_PKG_ACTIVE_GT_ENGINE_CYCLES_OFFSET),
        &state->mchbar_data.last_mem.active_gt_engine_cycles);
}

static ReadMiscResult read_mc_peci_therm_margin(PerfmonState* state) {
    uint32_t value = read_mc_value32(
                get_mc_addr32(state, MISC_PKG_PECI_THERM_MARGIN_OFFSET));
    return ReadMiscResult{value & 0xffff, CPUPERF_RECORD_VALUE};
}

static ReadMiscResult read_mc_rapl_perf_status(PerfmonState* state) {
    return read_mc_typed_value32(
        get_mc_addr32(state, MISC_PKG_RAPL_PERF_STATUS_OFFSET));
}

static ReadMiscResult read_mc_ia_freq_clamping_reasons(PerfmonState* state) {
    // Some of the reserved bits have read as ones. Remove them to make the
    // reported value easier to read.
    const uint32_t kReserved =
        (1u << 31) | (1u << 30) | (1u << 25) | (1u << 19) | (1u << 18) |
        (1u << 15) | (1u << 14) | (1u << 9)  | (1u << 3)  | (1u << 2);
    uint32_t value = read_mc_value32(
        get_mc_addr32(state, MISC_PKG_IA_FREQ_CLAMPING_REASONS_OFFSET));
    return ReadMiscResult{value & ~kReserved, CPUPERF_RECORD_VALUE};
}

static ReadMiscResult read_mc_gt_freq_clamping_reasons(PerfmonState* state) {
    // Some of the reserved bits have read as ones. Remove them to make the
    // reported value easier to read.
    const uint32_t kReserved =
        (1u << 31) | (1u << 30) | (1u << 29) | (1u << 25) | (1u << 20) |
        (1u << 19) | (1u << 18) | (1u << 15) | (1u << 14) | (1u << 13) |
        (1u << 9)  | (1u << 4)  | (1u << 3)  | (1u << 2);
    uint32_t value = read_mc_value32(
        get_mc_addr32(state, MISC_PKG_GT_FREQ_CLAMPING_REASONS_OFFSET));
    return ReadMiscResult{value & ~kReserved, CPUPERF_RECORD_VALUE};
}

static ReadMiscResult read_mc_rp_slice_freq(PerfmonState* state) {
    uint32_t value = read_mc_value32(
        get_mc_addr32(state, MISC_PKG_RP_SLICE_FREQ_OFFSET));
    value = (value >> 17) & 0x1ff;
    // Convert the value to Mhz.
    // We can't do floating point, and this doesn't have to be perfect.
    uint64_t scaled_value = value * 16667ul / 1000 /*16.667*/;
    return ReadMiscResult{scaled_value, CPUPERF_RECORD_VALUE};
}

static ReadMiscResult read_mc_rp_unslice_freq(PerfmonState* state) {
    uint32_t value = read_mc_value32(
        get_mc_addr32(state, MISC_PKG_RP_UNSLICE_FREQ_OFFSET));
    value = (value >> 8) & 0x1ff;
    // Convert the value to Mhz.
    // We can't do floating point, and this doesn't have to be perfect.
    uint64_t scaled_value = value * 16667ul / 1000 /*16.667*/;
    return ReadMiscResult{scaled_value, CPUPERF_RECORD_VALUE};
}

static ReadMiscResult read_mc_rp_volt(PerfmonState* state) {
    uint32_t value = read_mc_value32(
        get_mc_addr32(state, MISC_PKG_RP_VOLT_OFFSET));
    return ReadMiscResult{value & 0xff, CPUPERF_RECORD_VALUE};
}

static ReadMiscResult read_mc_edram_temp(PerfmonState* state) {
    uint32_t value = read_mc_value32(
        get_mc_addr32(state, MISC_PKG_EDRAM_TEMP_OFFSET));
    return ReadMiscResult{value & 0xff, CPUPERF_RECORD_VALUE};
}

static ReadMiscResult read_mc_pkg_temp(PerfmonState* state) {
    uint32_t value = read_mc_value32(
        get_mc_addr32(state, MISC_PKG_PKG_TEMP_OFFSET));
    return ReadMiscResult{value & 0xff, CPUPERF_RECORD_VALUE};
}

static ReadMiscResult read_mc_ia_temp(PerfmonState* state) {
    uint32_t value = read_mc_value32(
        get_mc_addr32(state, MISC_PKG_IA_TEMP_OFFSET));
    return ReadMiscResult{value & 0xff, CPUPERF_RECORD_VALUE};
}

static ReadMiscResult read_mc_gt_temp(PerfmonState* state) {
    uint32_t value = read_mc_value32(
        get_mc_addr32(state, MISC_PKG_GT_TEMP_OFFSET));
    return ReadMiscResult{value & 0xff, CPUPERF_RECORD_VALUE};
}

static ReadMiscResult read_misc_event(PerfmonState* state,
                                      cpuperf_event_id_t id) {
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
    case MISC_PKG_RP_SLICE_FREQ_ID:
        return read_mc_rp_slice_freq(state);
    case MISC_PKG_RP_UNSLICE_FREQ_ID:
        return read_mc_rp_unslice_freq(state);
    case MISC_PKG_RP_VOLT_ID:
        return read_mc_rp_volt(state);
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


static void x86_ipm_unmap_buffers_locked(PerfmonState* state) {
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
}

static zx_status_t x86_map_mchbar_stat_registers(PerfmonState* state) {
    DEBUG_ASSERT(perfmon_mchbar_bar != 0);
    fbl::RefPtr<VmObject> vmo;
    vaddr_t begin_page =
        (perfmon_mchbar_bar + UNC_IMC_STATS_BEGIN) & ~(PAGE_SIZE - 1);
    vaddr_t end_page =
        (perfmon_mchbar_bar + UNC_IMC_STATS_END) & ~(PAGE_SIZE - 1);
    size_t num_bytes_to_map = end_page + PAGE_SIZE - begin_page;
    size_t begin_offset =
        (perfmon_mchbar_bar + UNC_IMC_STATS_BEGIN) & (PAGE_SIZE - 1);

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
    status = vmar->CreateVmMapping(0, PAGE_SIZE, /*align_pow2*/0,
                                   vmar_flags, fbl::move(vmo),
                                   0, arch_mmu_flags, name,
                                   &mapping);
    if (status != ZX_OK)
        return status;

    status = mapping->MapRange(0, PAGE_SIZE, false);
    if (status != ZX_OK)
        return status;

    state->mchbar_data.mapping = mapping;
    state->mchbar_data.stats_addr =
            reinterpret_cast<void*>(mapping->base() + begin_offset);

    // Record the current values of these so that the trace will only include
    // the delta since tracing started.
#define INIT_MC_COUNT(member) \
    do { \
        state->mchbar_data.last_mem.member = 0; \
        (void) read_mc_ ## member(state); \
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

    TRACEF("memory stats mapped: begin 0x%lx, %zu bytes\n",
           mapping->base(), num_bytes_to_map);

    return ZX_OK;
}

static zx_status_t x86_ipm_map_buffers_locked(PerfmonState* state) {
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
            0 /* ignored */, size, 0 /* align pow2 */, 0 /* vmar flags */,
            data->buffer_vmo, vmo_offset, arch_mmu_flags, name,
            &data->buffer_mapping);
        if (status != ZX_OK) {
            TRACEF("error %d mapping buffer: cpu %u, size 0x%zx\n",
                   status, cpu, size);
            break;
        }
        // Pass true for |commit| so that we get our pages mapped up front.
        // Otherwise we'll need to allow for a page fault to happen in the
        // PMI handler.
        status = data->buffer_mapping->MapRange(vmo_offset, size, true);
        if (status != ZX_OK) {
            TRACEF("error %d mapping range: cpu %u, size 0x%zx\n",
                   status, cpu, size);
            data->buffer_mapping->Destroy();
            data->buffer_mapping.reset();
            break;
        }
        data->buffer_start = reinterpret_cast<cpuperf_buffer_header_t*>(
            data->buffer_mapping->base() + vmo_offset);
        data->buffer_end = reinterpret_cast<char*>(data->buffer_start) + size;
        TRACEF("buffer mapped: cpu %u, start %p, end %p\n",
               cpu, data->buffer_start, data->buffer_end);

        auto hdr = data->buffer_start;
        hdr->version = CPUPERF_BUFFER_VERSION;
        hdr->arch = CPUPERF_BUFFER_ARCH_X86_64;
        hdr->flags = 0;
        hdr->ticks_per_second = ticks_per_second();
        hdr->capture_end = sizeof(*hdr);
        data->buffer_next = reinterpret_cast<cpuperf_record_header_t*>(
            reinterpret_cast<char*>(data->buffer_start) + hdr->capture_end);
    }

    // Get access to MCHBAR stats if we can.
    if (status == ZX_OK && state->need_mchbar) {
        status = x86_map_mchbar_stat_registers(state);
    }

    if (status != ZX_OK) {
        x86_ipm_unmap_buffers_locked(state);
    }

    return status;
}

// This is invoked via mp_sync_exec which thread safety analysis cannot follow.
static void x86_ipm_start_cpu_task(void* raw_context) TA_NO_THREAD_SAFETY_ANALYSIS {
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
        write_msr(IA32_PERFEVTSEL_FIRST + i, state->events[i]);
    }

    write_msr(IA32_DEBUGCTL, state->debug_ctrl);

    apic_pmi_unmask();

    // Enable counters as late as possible so that our setup doesn't contribute
    // to the data.
    write_msr(IA32_PERF_GLOBAL_CTRL, state->global_ctrl);
}

// Begin collecting data.

zx_status_t x86_ipm_start() {
    fbl::AutoLock al(&perfmon_lock);

    if (!supports_perfmon)
        return ZX_ERR_NOT_SUPPORTED;
    if (atomic_load(&perfmon_active))
        return ZX_ERR_BAD_STATE;
    if (!perfmon_state)
        return ZX_ERR_BAD_STATE;

    // Sanity check the buffers and map them in.
    // This is deferred until now so that they are mapped in as minimally as
    // necessary.
    // TODO(dje): OTOH one might want to start/stop/start/stop/... and
    // continually mapping/unmapping will be painful. Revisit when things
    // settle down.
    auto state = perfmon_state.get();
    auto status = x86_ipm_map_buffers_locked(state);
    if (status != ZX_OK)
        return status;

    TRACEF("Enabling perfmon, %u fixed, %u programmable, %u misc\n",
           state->num_used_fixed, state->num_used_programmable,
           state->num_used_misc);
    if (LOCAL_TRACE) {
        LTRACEF("global ctrl: 0x%" PRIx64 ", fixed ctrl: 0x%" PRIx64 "\n",
                state->global_ctrl, state->fixed_ctrl);
        for (unsigned i = 0; i < state->num_used_fixed; ++i) {
            LTRACEF("fixed[%u]: num %u, initial 0x%" PRIx64 "\n",
                    i, state->fixed_hw_map[i], state->fixed_initial_value[i]);
        }
        for (unsigned i = 0; i < state->num_used_programmable; ++i) {
            LTRACEF("programmable[%u]: id 0x%x, initial 0x%" PRIx64 "\n",
                    i, state->programmable_ids[i],
                    state->programmable_initial_value[i]);
        }
    }

    ktrace(TAG_IPM_START, 0, 0, 0, 0);
    mp_sync_exec(MP_IPI_TARGET_ALL, 0, x86_ipm_start_cpu_task, state);
    atomic_store(&perfmon_active, true);
    return ZX_OK;
}

// This is invoked via mp_sync_exec which thread safety analysis cannot follow.
static void x86_ipm_stop_cpu_task(void* raw_context) TA_NO_THREAD_SAFETY_ANALYSIS {
    // Disable all counters ASAP.
    write_msr(IA32_PERF_GLOBAL_CTRL, 0);
    apic_pmi_mask();

    DEBUG_ASSERT(arch_ints_disabled());
    DEBUG_ASSERT(!atomic_load(&perfmon_active));
    DEBUG_ASSERT(raw_context);

    auto state = reinterpret_cast<PerfmonState*>(raw_context);
    auto cpu = arch_curr_cpu_num();
    auto data = &state->cpu_data[cpu];
    auto now = rdtsc();

    // Retrieve final event values and write into the trace buffer.

    if (data->buffer_start) {
        LTRACEF("Collecting last data for cpu %u\n", cpu);
        auto hdr = data->buffer_start;
        auto next = data->buffer_next;
        auto last =
            reinterpret_cast<cpuperf_record_header_t*>(data->buffer_end) - 1;

        next = x86_perfmon_write_time_record(next, CPUPERF_EVENT_ID_NONE, now);

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
            if (next > last) {
                hdr->flags |= CPUPERF_BUFFER_FLAG_FULL;
                break;
            }
            cpuperf_event_id_t id = state->programmable_ids[i];
            DEBUG_ASSERT(id != 0);
            uint64_t count = read_msr(IA32_PMC_FIRST + i);
            if (count >= state->programmable_initial_value[i]) {
                count -= state->programmable_initial_value[i];
            } else {
                // The max counter value is generally not 64 bits.
                count += (perfmon_max_programmable_counter_value -
                          state->programmable_initial_value[i] + 1);
            }
            next = x86_perfmon_write_count_record(next, id, count);
        }
        for (unsigned i = 0; i < state->num_used_fixed; ++i) {
            if (next > last) {
                hdr->flags |= CPUPERF_BUFFER_FLAG_FULL;
                break;
            }
            cpuperf_event_id_t id = state->fixed_ids[i];
            DEBUG_ASSERT(id != 0);
            unsigned hw_num = state->fixed_hw_map[i];
            DEBUG_ASSERT(hw_num < perfmon_num_fixed_counters);
            uint64_t count = read_msr(IA32_FIXED_CTR0 + hw_num);
            if (count >= state->fixed_initial_value[i]) {
                count -= state->fixed_initial_value[i];
            } else {
                // The max counter value is generally not 64 bits.
                count += (perfmon_max_fixed_counter_value -
                          state->fixed_initial_value[i] + 1);
            }
            next = x86_perfmon_write_count_record(next, id, count);
        }
        // Misc events are currently all non-cpu-specific.
        // Just report for cpu 0. See pmi_interrupt_handler.
        if (cpu == 0) {
            for (unsigned i = 0; i < state->num_used_misc; ++i) {
                if (next > last) {
                    hdr->flags |= CPUPERF_BUFFER_FLAG_FULL;
                    break;
                }
                cpuperf_event_id_t id = state->misc_ids[i];
                ReadMiscResult typed_value = read_misc_event(state, id);
                switch (typed_value.type) {
                case CPUPERF_RECORD_COUNT:
                    next = x86_perfmon_write_count_record(next, id, typed_value.value);
                    break;
                case CPUPERF_RECORD_VALUE:
                    next = x86_perfmon_write_value_record(next, id, typed_value.value);
                    break;
                default:
                    __UNREACHABLE;
                }
            }
        }

        data->buffer_next = next;
        hdr->capture_end =
            reinterpret_cast<char*>(data->buffer_next) -
            reinterpret_cast<char*>(data->buffer_start);

        if (hdr->flags & CPUPERF_BUFFER_FLAG_FULL) {
            LTRACEF("Buffer overflow on cpu %u\n", cpu);
        }
    }

    x86_perfmon_clear_overflow_indicators();
}

// Stop collecting data.
// It's ok to call this multiple times.
// Returns an error if called before ALLOC or after FREE.
zx_status_t x86_ipm_stop() {
    fbl::AutoLock al(&perfmon_lock);

    if (!supports_perfmon)
        return ZX_ERR_NOT_SUPPORTED;
    if (!perfmon_state)
        return ZX_ERR_BAD_STATE;

    TRACEF("Disabling perfmon\n");

    // Do this before anything else so that any PMI interrupts from this point
    // on won't try to access potentially unmapped memory.
    atomic_store(&perfmon_active, false);

    // TODO(dje): Check clobbering of values - user should be able to do
    // multiple stops and still read register values.

    auto state = perfmon_state.get();
    mp_sync_exec(MP_IPI_TARGET_ALL, 0, x86_ipm_stop_cpu_task, state);
    ktrace(TAG_IPM_STOP, 0, 0, 0, 0);

    // x86_ipm_start currently maps the buffers in, so we unmap them here.
    // Make sure to do this after we've turned everything off so that we
    // don't get another PMI after this.
    x86_ipm_unmap_buffers_locked(state);

    return ZX_OK;
}

// Worker for x86_ipm_fini to be executed on all cpus.
// This is invoked via mp_sync_exec which thread safety analysis cannot follow.
static void x86_ipm_reset_task(void* raw_context) TA_NO_THREAD_SAFETY_ANALYSIS {
    DEBUG_ASSERT(arch_ints_disabled());
    DEBUG_ASSERT(!atomic_load(&perfmon_active));
    DEBUG_ASSERT(!raw_context);

    write_msr(IA32_PERF_GLOBAL_CTRL, 0);
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
// everything x86_ipm_init did.
// Must be called while tracing is stopped.
// It's ok to call this multiple times.
zx_status_t x86_ipm_fini() {
    fbl::AutoLock al(&perfmon_lock);

    if (!supports_perfmon)
        return ZX_ERR_NOT_SUPPORTED;
    if (atomic_load(&perfmon_active))
        return ZX_ERR_BAD_STATE;

    mp_sync_exec(MP_IPI_TARGET_ALL, 0, x86_ipm_reset_task, nullptr);

    perfmon_state.reset();

    return ZX_OK;
}

// Interrupt handling.

// Helper function so that there is only one place where we enable/disable
// interrupts (our caller).
// Returns true if success, false if buffer is full.

static bool pmi_interrupt_handler(x86_iframe_t *frame, PerfmonState* state) {
    // This is done here instead of in the caller so that it is done *after*
    // we disable the counters.
    CPU_STATS_INC(perf_ints);

    uint cpu = arch_curr_cpu_num();
    auto data = &state->cpu_data[cpu];

    // On x86 zx_ticks_get uses rdtsc.
    zx_time_t now = rdtsc();
    LTRACEF("cpu %u: now %" PRIu64 ", sp %p\n", cpu, now, __GET_FRAME());

    // Rather than continually checking if we have enough space, just
    // conservatively check for the maximum amount we'll need.
    size_t space_needed = (sizeof(cpuperf_time_record_t) +
                           (state->num_used_programmable +
                            state->num_used_fixed +
                            state->num_used_misc) *
                           kMaxEventRecordSize);
    if (reinterpret_cast<char*>(data->buffer_next) + space_needed > data->buffer_end) {
        TRACEF("cpu %u: @%" PRIu64 " pmi buffer full\n", cpu, now);
        data->buffer_start->flags |= CPUPERF_BUFFER_FLAG_FULL;
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

        next = x86_perfmon_write_time_record(next, CPUPERF_EVENT_ID_NONE, now);

        // Note: We don't write "value" records here instead prefering the
        // smaller "tick" record. If the user is tallying the counts the user
        // is required to recognize this and apply the tick rate.
        // TODO(dje): Precompute mask to detect whether the interrupt is for
        // the timebase counter, and then combine the loops.

        for (unsigned i = 0; i < state->num_used_programmable; ++i) {
            if (!(status & IA32_PERF_GLOBAL_STATUS_PMC_OVF_MASK(i)))
                continue;
            cpuperf_event_id_t id = state->programmable_ids[i];
            // Counters using a separate timebase are handled below.
            // We shouldn't get an interrupt on a counter using a timebase.
            // TODO(dje): The counter could still overflow. Later.
            if (id == state->timebase_id) {
                saw_timebase = true;
            } else if (state->programmable_flags[i] & IPM_CONFIG_FLAG_TIMEBASE) {
                continue;
            }
            if (state->programmable_flags[i] & IPM_CONFIG_FLAG_PC) {
                next = x86_perfmon_write_pc_record(next, id, cr3, frame->ip);
            } else {
                next = x86_perfmon_write_tick_record(next, id);
            }
            LTRACEF("cpu %u: resetting PMC %u to 0x%" PRIx64 "\n",
                    cpu, i, state->programmable_initial_value[i]);
            write_msr(IA32_PMC_FIRST + i, state->programmable_initial_value[i]);
        }

        for (unsigned i = 0; i < state->num_used_fixed; ++i) {
            unsigned hw_num = state->fixed_hw_map[i];
            DEBUG_ASSERT(hw_num < perfmon_num_fixed_counters);
            if (!(status & IA32_PERF_GLOBAL_STATUS_FIXED_OVF_MASK(hw_num)))
                continue;
            cpuperf_event_id_t id = state->fixed_ids[i];
            // Counters using a separate timebase are handled below.
            // We shouldn't get an interrupt on a counter using a timebase.
            // TODO(dje): The counter could still overflow. Later.
            if (id == state->timebase_id) {
                saw_timebase = true;
            } else if (state->fixed_flags[i] & IPM_CONFIG_FLAG_TIMEBASE) {
                continue;
            }
            if (state->fixed_flags[i] & IPM_CONFIG_FLAG_PC) {
                next = x86_perfmon_write_pc_record(next, id, cr3, frame->ip);
            } else {
                next = x86_perfmon_write_tick_record(next, id);
            }
            LTRACEF("cpu %u: resetting FIXED %u to 0x%" PRIx64 "\n",
                    cpu, hw_num, state->fixed_initial_value[i]);
            write_msr(IA32_FIXED_CTR0 + hw_num, state->fixed_initial_value[i]);
        }

        bits_to_clear |= perfmon_counter_status_bits;

        // Now handle events that have IPM_CONFIG_FLAG_TIMEBASE set.
        if (saw_timebase) {
            for (unsigned i = 0; i < state->num_used_programmable; ++i) {
                if (!(state->programmable_flags[i] & IPM_CONFIG_FLAG_TIMEBASE))
                    continue;
                cpuperf_event_id_t id = state->programmable_ids[i];
                uint64_t count = read_msr(IA32_PMC_FIRST + i);
                next = x86_perfmon_write_count_record(next, id, count);
                // We could leave the counter alone, but it could overflow.
                // Instead reduce the risk and just always reset to zero.
                LTRACEF("cpu %u: resetting PMC %u to 0x%" PRIx64 "\n",
                        cpu, i, state->programmable_initial_value[i]);
                write_msr(IA32_PMC_FIRST + i, state->programmable_initial_value[i]);
            }
            for (unsigned i = 0; i < state->num_used_fixed; ++i) {
                if (!(state->fixed_flags[i] & IPM_CONFIG_FLAG_TIMEBASE))
                    continue;
                cpuperf_event_id_t id = state->fixed_ids[i];
                unsigned hw_num = state->fixed_hw_map[i];
                DEBUG_ASSERT(hw_num < perfmon_num_fixed_counters);
                uint64_t count = read_msr(IA32_FIXED_CTR0 + hw_num);
                next = x86_perfmon_write_count_record(next, id, count);
                // We could leave the counter alone, but it could overflow.
                // Instead reduce the risk and just always reset to zero.
                LTRACEF("cpu %u: resetting FIXED %u to 0x%" PRIx64 "\n",
                        cpu, hw_num, state->fixed_initial_value[i]);
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
                    if (!(state->misc_flags[i] & IPM_CONFIG_FLAG_TIMEBASE)) {
                        // While a timebase is required for all current misc
                        // counters, we don't assume this here.
                        continue;
                    }
                    cpuperf_event_id_t id = state->misc_ids[i];
                    ReadMiscResult typed_value = read_misc_event(state, id);
                    switch (typed_value.type) {
                    case CPUPERF_RECORD_COUNT:
                        next = x86_perfmon_write_count_record(next, id, typed_value.value);
                        break;
                    case CPUPERF_RECORD_VALUE:
                        next = x86_perfmon_write_value_record(next, id, typed_value.value);
                        break;
                    default:
                        __UNREACHABLE;
                    }
                }
            }
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
    bits_to_clear |= (IA32_PERF_GLOBAL_STATUS_UNCORE_OVF_MASK |
                      IA32_PERF_GLOBAL_STATUS_COND_CHGD_MASK);

    // TODO(dje): No need to accumulate bits to clear if we're going to clear
    // everything that's set anyway. Kept as is during development.
    bits_to_clear |= status;

    LTRACEF("cpu %u: clearing status bits 0x%" PRIx64 "\n",
            cpu, bits_to_clear);
    write_msr(IA32_PERF_GLOBAL_STATUS_RESET, bits_to_clear);

    // TODO(dje): Always do this test for now. Later conditionally include
    // via some debugging macro.
    uint64_t end_status = read_msr(IA32_PERF_GLOBAL_STATUS);
    if (end_status != 0)
        TRACEF("WARNING: cpu %u: end status 0x%" PRIx64 "\n", cpu, end_status);

    return true;
}

void apic_pmi_interrupt_handler(x86_iframe_t *frame) TA_NO_THREAD_SAFETY_ANALYSIS {
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
    write_msr(IA32_PERF_GLOBAL_CTRL, 0);
#endif

    DEBUG_ASSERT(arch_ints_disabled());

    auto state = perfmon_state.get();

#if 0
    // TODO(dje): We may want this anyway. If we want to be able to handle
    // page faults inside this handler we'll need to turn interrupts back
    // on. At the moment we can't do this as we don't handle recursive PMIs.
    arch_set_in_int_handler(false);
    arch_enable_ints();
#endif

    bool success = pmi_interrupt_handler(frame, state);

#if 0
    arch_disable_ints();
    arch_set_in_int_handler(true);
#endif

    // This is done here instead of in the caller so that we have full control
    // of when counting is restored.
    apic_issue_eoi();

    // If buffer is full leave everything turned off.
    if (!success) {
#if TRY_FREEZE_ON_PMI
        write_msr(IA32_PERF_GLOBAL_CTRL, 0);
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
        write_msr(IA32_PERF_GLOBAL_CTRL, state->global_ctrl);
#endif
    }
}
