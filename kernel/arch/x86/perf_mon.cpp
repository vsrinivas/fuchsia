// Copyright 2017 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

// TODO(dje): wip
// The thought is to use resources (as in ResourceDispatcher), at which point
// this will all get rewritten. Until such time, the goal here is KISS.
// This file contains the lower part of Intel Performance Monitor support that
// must be done in the kernel (so that we can read/write msrs).
// The userspace driver is in system/dev/misc/intel-pm/intel-pm.c.

// TODO(dje): See Intel Vol 3 18.2.3.1 for hypervisor recommendations.
// TODO(dje): LBR, BTS, et.al. See Intel Vol 3 Chapter 17.
// TODO(dje): PMI mitigations
// TODO(dje): Eventually may wish to virtualize some/all of the MSRs,
//            some have multiple disparate uses.
// TODO(dje): vmo management
// TODO(dje): check hyperthread handling

#include <arch/arch_ops.h>
#include <arch/mmu.h>
#include <arch/x86.h>
#include <arch/x86/apic.h>
#include <arch/x86/feature.h>
#include <arch/x86/mmu.h>
#include <arch/x86/perf_mon.h>
#include <err.h>
#include <fbl/alloc_checker.h>
#include <fbl/macros.h>
#include <fbl/ref_ptr.h>
#include <fbl/unique_ptr.h>
#include <kernel/mp.h>
#include <kernel/mutex.h>
#include <kernel/stats.h>
#include <kernel/thread.h>
#include <platform.h>
#include <vm/vm.h>
#include <vm/vm_address_region.h>
#include <vm/vm_aspace.h>
#include <lib/ktrace.h>
#include <zircon/device/cpu-trace/intel-pm.h>
#include <zircon/ktrace.h>
#include <zircon/mtrace.h>
#include <zircon/thread_annotations.h>
#include <pow2.h>
#include <string.h>
#include <trace.h>

#define LOCAL_TRACE 0

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

// These aren't constexpr as we iterate to fill in values for each counter.
static uint64_t kGlobalCtrlWritableBits;
static uint64_t kFixedCounterCtrlWritableBits;

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

// Counter bits in GLOBAL_STATUS to check on each interrupt.
static uint64_t perfmon_counter_status_bits = 0;

struct perfmon_cpu_data_t {
    // IA32_PMC_*
    uint64_t programmable_counters[IPM_MAX_PROGRAMMABLE_COUNTERS] = {};

    // IA32_FIXED_CTR*
    uint64_t fixed_counters[IPM_MAX_FIXED_COUNTERS] = {};

    // The trace buffer, passed in from userspace.
    fbl::RefPtr<VmObject> buffer_vmo;
    uint64_t start_offset = 0;
    uint64_t end_offset = 0;

    // The trace buffer when mapped into kernel space.
    // This is only done while the trace is running.
    fbl::RefPtr<VmMapping> buffer_mapping;
    void* buffer_start = 0;
    void* buffer_end = 0;

    // In sampling mode, the next record to fill.
    zx_x86_ipm_sample_record_t* buffer_next = nullptr;
};

struct perfmon_state_t {
    // IA32_PERF_GLOBAL_CTRL
    uint64_t global_ctrl = 0;

    // IA32_PERFEVTSEL_*
    uint64_t events[IPM_MAX_PROGRAMMABLE_COUNTERS] = {};

    // IA32_FIXED_CTR_CTRL
    uint64_t fixed_counter_ctrl = 0;

    // IA32_DEBUGCTL
    uint64_t debug_ctrl = 0;

    // The sample frequency or zero for "counting mode".
    // TODO(dje): Move more intelligence into the driver.
    uint32_t sample_freq = 0;

    // The counters are reset to this when they overflow.
    uint64_t programmable_initial_value = 0;
    uint64_t fixed_initial_value = 0;

    // An array with one entry for each cpu.
    fbl::unique_ptr<perfmon_cpu_data_t[]> cpu_data;
};

static fbl::Mutex perfmon_lock;

static fbl::unique_ptr<perfmon_state_t> perfmon_state TA_GUARDED(perfmon_lock);

// This is accessed atomically as it is also accessed by the PMI handler.
static int perfmon_active = false;

static unsigned ipm_num_cpus() { return arch_max_num_cpus(); }

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

    TRACEF("perfmon: version: %u\n", perfmon_version);
    TRACEF("perfmon: num_programmable_counters: %u\n", perfmon_num_programmable_counters);
    TRACEF("perfmon: programmable_counter_width: %u\n", perfmon_programmable_counter_width);
    TRACEF("perfmon: num_fixed_counters: %u\n", perfmon_num_fixed_counters);
    TRACEF("perfmon: fixed_counter_width: %u\n", perfmon_fixed_counter_width);
    TRACEF("perfmon: unsupported counters: 0x%x\n", perfmon_unsupported_events);
    TRACEF("perfmon: capabilities: 0x%x\n", perfmon_capabilities);
}

static void x86_perfmon_clear_overflow_indicators() {
    uint64_t value = (IA32_PERF_GLOBAL_OVF_CTRL_CLR_COND_CHGD_MASK |
                      IA32_PERF_GLOBAL_OVF_CTRL_DS_BUFFER_CLR_OVF_MASK |
                      IA32_PERF_GLOBAL_OVF_CTRL_UNCORE_CLR_OVF_MASK);

    for (unsigned i = 0; i < perfmon_num_programmable_counters; ++i) {
        value |= IA32_PERF_GLOBAL_OVF_CTRL_PMC_CLR_OVF_MASK(i);
    }

    for (unsigned i = 0; i < perfmon_num_fixed_counters; ++i) {
        value |= IA32_PERF_GLOBAL_OVF_CTRL_FIXED_CTR_CLR_OVF_MASK(i);
    }

    write_msr(IA32_PERF_GLOBAL_OVF_CTRL, value);
}

zx_status_t x86_ipm_get_state(zx_x86_ipm_state_t* state) {
    fbl::AutoLock al(&perfmon_lock);

    if (!supports_perfmon)
        return ZX_ERR_NOT_SUPPORTED;
    state->api_version = IPM_API_VERSION;
    state->pm_version = perfmon_version;
    state->num_fixed_counters = perfmon_num_fixed_counters;
    state->num_programmable_counters = perfmon_num_programmable_counters;
    state->perf_capabilities = perfmon_capabilities;
    state->alloced = !!perfmon_state;
    state->started = atomic_load(&perfmon_active);
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

    fbl::unique_ptr<perfmon_cpu_data_t[]> cpu_data;
    {
        unsigned num_cpus = ipm_num_cpus();
        fbl::AllocChecker ac;
        cpu_data.reset(new (&ac) perfmon_cpu_data_t[num_cpus]);
        if (!ac.check())
            return ZX_ERR_NO_MEMORY;
    }
    {
        fbl::AllocChecker ac;
        perfmon_state.reset(new (&ac) perfmon_state_t);
        if (!ac.check())
            return ZX_ERR_NO_MEMORY;
        perfmon_state->cpu_data = fbl::move(cpu_data);
    }

    return ZX_OK;
}

zx_status_t x86_ipm_assign_buffer(uint32_t cpu, fbl::RefPtr<VmObject> vmo,
                                  const zx_x86_ipm_buffer_t* buffer) {
    fbl::AutoLock al(&perfmon_lock);

    if (!supports_perfmon)
        return ZX_ERR_NOT_SUPPORTED;
    if (atomic_load(&perfmon_active))
        return ZX_ERR_BAD_STATE;
    if (!perfmon_state)
        return ZX_ERR_BAD_STATE;
    unsigned num_cpus = ipm_num_cpus();
    if (cpu >= num_cpus)
        return ZX_ERR_INVALID_ARGS;

    // TODO(dje): KISS for now
    if (buffer->start_offset != 0)
        return ZX_ERR_INVALID_ARGS;
    const size_t size = buffer->end_offset - buffer->start_offset;
    if (size > vmo->size())
        return ZX_ERR_INVALID_ARGS;
    if (size < sizeof(zx_x86_ipm_counters_t))
        return ZX_ERR_INVALID_ARGS;

    auto data = &perfmon_state->cpu_data[cpu];
    data->buffer_vmo = vmo;
    data->start_offset = buffer->start_offset;
    data->end_offset = buffer->end_offset;
    // The buffer is mapped into kernelspace later.

    return ZX_OK;
}

// Turn on/off sampling mode enable bits.

static void x86_ipm_set_sampling_mode_locked(perfmon_state_t* state, bool enable) {
    for (unsigned i = 0; i < perfmon_num_programmable_counters; ++i) {
        if (enable) {
            state->events[i] |= IA32_PERFEVTSEL_INT_MASK;
        } else {
            state->events[i] &= ~IA32_PERFEVTSEL_INT_MASK;
        }
    }
    for (unsigned i = 0; i < perfmon_num_fixed_counters; ++i) {
        if (enable) {
            state->fixed_counter_ctrl |= IA32_FIXED_CTR_CTRL_PMI_MASK(i);
        } else {
            state->fixed_counter_ctrl &= ~IA32_FIXED_CTR_CTRL_PMI_MASK(i);
        }
    }
}

zx_status_t x86_ipm_stage_config(const zx_x86_ipm_perf_config_t* config) {
    fbl::AutoLock al(&perfmon_lock);

    if (!supports_perfmon)
        return ZX_ERR_NOT_SUPPORTED;
    if (atomic_load(&perfmon_active))
        return ZX_ERR_BAD_STATE;
    if (!perfmon_state)
        return ZX_ERR_BAD_STATE;

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
    if (config->fixed_counter_ctrl & ~kFixedCounterCtrlWritableBits) {
        TRACEF("Non writable bits set in |fixed_counter_ctrl|\n");
        return ZX_ERR_INVALID_ARGS;
    }
    if (config->debug_ctrl & ~kDebugCtrlWritableBits) {
        TRACEF("Non writable bits set in |debug_ctrl|\n");
        return ZX_ERR_INVALID_ARGS;
    }
    for (unsigned i = 0; i < perfmon_num_programmable_counters; ++i) {
        if (config->programmable_events[i] & ~kEventSelectWritableBits) {
            TRACEF("Non writable bits set in |programmable_events[%u]|\n", i);
            return ZX_ERR_INVALID_ARGS;
        }
    }
    uint64_t max_programmable_counter_value = ~0ul;
    if (perfmon_programmable_counter_width < 64)
        max_programmable_counter_value = (1ul << perfmon_programmable_counter_width) - 1;
    uint64_t max_fixed_counter_value = ~0ul;
    if (perfmon_fixed_counter_width < 64)
        max_fixed_counter_value = (1ul << perfmon_fixed_counter_width) - 1;
    if (config->sample_freq > max_programmable_counter_value ||
        config->sample_freq > max_fixed_counter_value) {
        TRACEF("|sample_freq| is larger than the max counter value\n");
        return ZX_ERR_INVALID_ARGS;
    }

    unsigned num_cpus = ipm_num_cpus();

    perfmon_state->global_ctrl = config->global_ctrl;
    DEBUG_ASSERT(sizeof(perfmon_state->events) == sizeof(config->programmable_events));
    memcpy(perfmon_state->events, config->programmable_events, sizeof(perfmon_state->events));
    perfmon_state->fixed_counter_ctrl = config->fixed_counter_ctrl;
    perfmon_state->debug_ctrl = config->debug_ctrl;
    perfmon_state->sample_freq = config->sample_freq;

    if (perfmon_state->sample_freq == 0) {
        x86_ipm_set_sampling_mode_locked(perfmon_state.get(), false);
    } else {
        x86_ipm_set_sampling_mode_locked(perfmon_state.get(), true);
        // TODO(dje): The counter widths aren't 64 bits, and may never be,
        // so we could punt and simplify all this until then.
        DEBUG_ASSERT(perfmon_programmable_counter_width <= 64);
        if (perfmon_programmable_counter_width == 64) {
            perfmon_state->programmable_initial_value =
                ~0ul - perfmon_state->sample_freq + 1;
        } else {
            DEBUG_ASSERT(perfmon_state->sample_freq < (1ul << perfmon_programmable_counter_width));
            perfmon_state->programmable_initial_value =
                (1ul << perfmon_programmable_counter_width) - perfmon_state->sample_freq;
        }
        DEBUG_ASSERT(perfmon_fixed_counter_width <= 64);
        if (perfmon_fixed_counter_width == 64) {
            perfmon_state->fixed_initial_value =
                ~0ul - perfmon_state->sample_freq + 1;
        } else {
            DEBUG_ASSERT(perfmon_state->sample_freq < (1ul << perfmon_fixed_counter_width));
            perfmon_state->fixed_initial_value =
                (1ul << perfmon_fixed_counter_width) - perfmon_state->sample_freq;
        }
    }

    for (unsigned i = 0; i < num_cpus; ++i) {
        perfmon_cpu_data_t* data = &perfmon_state->cpu_data[i];
        memset(data->programmable_counters, 0, sizeof(data->programmable_counters));
        memset(data->fixed_counters, 0, sizeof(data->fixed_counters));
        if (perfmon_state->sample_freq != 0) {
            for (unsigned j = 0; j < perfmon_num_programmable_counters; ++j)
                data->programmable_counters[j] = perfmon_state->programmable_initial_value;
            for (unsigned j = 0; j < perfmon_num_fixed_counters; ++j)
                data->fixed_counters[j] = perfmon_state->fixed_initial_value;
        }
    }

    return ZX_OK;
}

static void x86_ipm_unmap_buffers_locked(perfmon_state_t* state) {
    unsigned num_cpus = ipm_num_cpus();
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
}

static zx_status_t x86_ipm_map_buffers_locked(perfmon_state_t* state) {
    unsigned num_cpus = ipm_num_cpus();
    zx_status_t status = ZX_OK;
    for (unsigned cpu = 0; cpu < num_cpus; ++cpu) {
        auto data = &state->cpu_data[cpu];
        // Heads up: The logic is off if this isn't true.
        DEBUG_ASSERT(data->start_offset == 0);
        const size_t size = data->end_offset - data->start_offset;
        const uint arch_mmu_flags = ARCH_MMU_FLAG_PERM_READ | ARCH_MMU_FLAG_PERM_WRITE;
        const char* name = "ipm-buffer";
        status = VmAspace::kernel_aspace()->RootVmar()->CreateVmMapping(
            0 /* ignored */, size, 0 /* align pow2 */, 0 /* vmar flags */,
            data->buffer_vmo, data->start_offset, arch_mmu_flags, name,
            &data->buffer_mapping);
        if (status != ZX_OK) {
            TRACEF("error %d mapping buffer: cpu %u, start 0x%" PRIx64 ", end 0x%" PRIx64 "\n",
                   status, cpu, data->start_offset, data->end_offset);
            break;
        }
        // Pass true for |commit| so that we get our pages mapped up front.
        // Otherwise we'll need to allow for a page fault to happen in the
        // PMI handler.
        status = data->buffer_mapping->MapRange(data->start_offset, size, true);
        if (status != ZX_OK) {
            TRACEF("error %d mapping range: cpu %u, start 0x%" PRIx64 ", end 0x%" PRIx64 "\n",
                   status, cpu, data->start_offset, data->end_offset);
            data->buffer_mapping->Destroy();
            data->buffer_mapping.reset();
            break;
        }
        data->buffer_start = reinterpret_cast<void*>(
            data->buffer_mapping->base() + data->start_offset);
        data->buffer_end = reinterpret_cast<char*>(data->buffer_start) + size;
        TRACEF("buffer mapped: cpu %u, start %p, end %p\n",
               cpu, data->buffer_start, data->buffer_end);

        auto info = reinterpret_cast<zx_x86_ipm_buffer_info_t*>(data->buffer_start);
        info->version = (state->sample_freq != 0
                         ? IPM_BUFFER_SAMPLING_MODE_VERSION
                         : IPM_BUFFER_COUNTING_MODE_VERSION);
        info->padding = 0;
        info->ticks_per_second = ticks_per_second();
        info->capture_end = sizeof(*info);
        data->buffer_next = reinterpret_cast<zx_x86_ipm_sample_record_t*>(
            reinterpret_cast<char*>(data->buffer_start) + info->capture_end);
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

    perfmon_state_t* state = reinterpret_cast<perfmon_state_t*>(raw_context);
    uint32_t cpu = arch_curr_cpu_num();
    perfmon_cpu_data_t* data = &state->cpu_data[cpu];

    for (unsigned i = 0; i < perfmon_num_fixed_counters; ++i) {
        write_msr(IA32_FIXED_CTR0 + i, data->fixed_counters[i]);
    }
    write_msr(IA32_FIXED_CTR_CTRL, state->fixed_counter_ctrl);

    for (unsigned i = 0; i < perfmon_num_programmable_counters; ++i) {
        // Ensure PERFEVTSEL.EN is zero before resetting the counter value,
        // h/w requires it (apparently even if global ctrl is off).
        write_msr(IA32_PERFEVTSEL_FIRST + i, 0);
        // The counter must be written before PERFEVTSEL.EN is set to 1.
        write_msr(IA32_PMC_FIRST + i, data->programmable_counters[i]);
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
    auto status = x86_ipm_map_buffers_locked(perfmon_state.get());
    if (status != ZX_OK)
        return status;

    TRACEF("Enabling perfmon\n");

    ktrace(TAG_IPM_START, 0, 0, 0, 0);
    mp_sync_exec(MP_IPI_TARGET_ALL, 0, x86_ipm_start_cpu_task, perfmon_state.get());
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

    auto state = reinterpret_cast<perfmon_state_t*>(raw_context);
    uint32_t cpu = arch_curr_cpu_num();
    auto data = &state->cpu_data[cpu];

    // Retrieve msr values and write into the trace buffer.
    // TODO(dje): Need to ensure these writes won't fail.

    if (data->buffer_start) {
        auto info = reinterpret_cast<zx_x86_ipm_buffer_info_t*>(data->buffer_start);
        if (perfmon_state->sample_freq == 0) {
            auto buffer = reinterpret_cast<zx_x86_ipm_counters_t*>(
                data->buffer_next);
            buffer->status = read_msr(IA32_PERF_GLOBAL_STATUS);
            buffer->time = rdtsc();
            for (unsigned i = 0; i < perfmon_num_fixed_counters; ++i) {
                buffer->fixed_counters[i] = read_msr(IA32_FIXED_CTR0 + i);
            }
            for (unsigned i = 0; i < perfmon_num_programmable_counters; ++i) {
                buffer->programmable_counters[i] = read_msr(IA32_PMC_FIRST + i);
            }
            info->capture_end += sizeof(*buffer);
        } else {
            info->capture_end =
                reinterpret_cast<char*>(data->buffer_next) -
                reinterpret_cast<char*>(data->buffer_start);
        }
    }

    x86_perfmon_clear_overflow_indicators();
}

// Stop collecting data.
// It's ok to call this multiple times.
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

    mp_sync_exec(MP_IPI_TARGET_ALL, 0, x86_ipm_stop_cpu_task, perfmon_state.get());
    ktrace(TAG_IPM_STOP, 0, 0, 0, 0);

    // TODO(dje): Fetch last value of counters.

    // x86_ipm_start currently maps the buffers in, so we unmap them here.
    // Make sure to do this after we've turned everything off so that we
    // don't get another PMI after this.
    x86_ipm_unmap_buffers_locked(perfmon_state.get());

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

static void write_record(zx_x86_ipm_sample_record_t* rec,
                         zx_time_t time, uint32_t counter, uint64_t pc) {
    rec->time = time;
    rec->counter = counter;
    rec->pc = pc;
}

// Helper function so that there is only one place where we enable/disable
// interrupts (our caller).
// Returns true if success, false if buffer is full.

static bool pmi_interrupt_handler(x86_iframe_t *frame, perfmon_state_t* state) {
    // This is done here instead of in the caller so that it is done *after*
    // we disable the counters.
    CPU_STATS_INC(perf_ints);

    uint cpu = arch_curr_cpu_num();
    perfmon_cpu_data_t* data = &state->cpu_data[cpu];

    // On x86 zx_ticks_get uses rdtsc.
    zx_time_t now = rdtsc();
    LTRACEF("cpu %u: now %" PRIu64 ", sp %p\n", cpu, now, __GET_FRAME());

    // Rather than continually checking if we have enough space, just check
    // for the maximum amount we'll need.
    size_t space_needed =
        (perfmon_num_programmable_counters + perfmon_num_fixed_counters) *
        sizeof(*data->buffer_next);
    if (reinterpret_cast<char*>(data->buffer_next) + space_needed > data->buffer_end) {
        TRACEF("cpu %u: @%" PRIu64 " pmi buffer full\n", cpu, now);
        return false;
    }

    const uint64_t status = read_msr(IA32_PERF_GLOBAL_STATUS);
    uint64_t bits_to_clear = 0;

    LTRACEF("cpu %u: status 0x%" PRIx64 "\n", cpu, status);

    if (status & perfmon_counter_status_bits) {
#if TRY_FREEZE_ON_PMI
        if (!(status & IA32_PERF_GLOBAL_STATUS_CTR_FRZ_MASK))
            LTRACEF("Eh? status.CTR_FRZ not set\n");
#else
        if (status & IA32_PERF_GLOBAL_STATUS_CTR_FRZ_MASK)
            LTRACEF("Eh? status.CTR_FRZ is set\n");
#endif

        zx_x86_ipm_sample_record_t* next = data->buffer_next;

        for (unsigned i = 0; i < perfmon_num_programmable_counters; ++i) {
            if (status & IA32_PERF_GLOBAL_STATUS_PMC_OVF_MASK(i)) {
                write_record(next, now, i, frame->ip);
                ++next;
                LTRACEF("cpu %u: resetting PMC %u to 0x%" PRIx64 "\n",
                        cpu, i, state->programmable_initial_value);
                write_msr(IA32_PMC_FIRST + i, state->programmable_initial_value);
            }
        }

        for (unsigned i = 0; i < perfmon_num_fixed_counters; ++i) {
            if (status & IA32_PERF_GLOBAL_STATUS_FIXED_OVF_MASK(i)) {
                write_record(next, now, i | IPM_COUNTER_NUMBER_FIXED, frame->ip);
                ++next;
                LTRACEF("cpu %u: resetting FIXED %u to 0x%" PRIx64 "\n",
                        cpu, i, state->programmable_initial_value);
                write_msr(IA32_FIXED_CTR0 + i, state->programmable_initial_value);
            }
        }

        bits_to_clear |= perfmon_counter_status_bits;

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
        TRACEF("cpu %u: end status 0x%" PRIx64 "\n", cpu, end_status);

    return true;
}

enum handler_return apic_pmi_interrupt_handler(x86_iframe_t *frame) TA_NO_THREAD_SAFETY_ANALYSIS {
    if (!atomic_load(&perfmon_active)) {
        apic_issue_eoi();
        return INT_NO_RESCHEDULE;
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

    perfmon_state_t* state = perfmon_state.get();

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

    return INT_NO_RESCHEDULE;
}
