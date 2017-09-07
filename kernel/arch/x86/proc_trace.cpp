// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

// TODO(MG-992): Need to be able to r/w MSRs.
// The thought is to use resources (as in ResourceDispatcher), at which point
// this will all get rewritten. Until such time, the goal here is KISS.
// This file contains the lower part of Intel Processor Trace support that must
// be done in the kernel (so that we can read/write msrs).
// The userspace driver is in system/udev/intel-pt/intel-pt.c.
//
// We currently only support Table of Physical Addresses mode:
// it supports discontiguous buffers and supports stop-on-full behavior
// in addition to wrap-around.

#include <arch/arch_ops.h>
#include <arch/mmu.h>
#include <arch/x86.h>
#include <arch/x86/feature.h>
#include <arch/x86/mmu.h>
#include <arch/x86/proc_trace.h>
#include <err.h>
#include <kernel/mp.h>
#include <kernel/thread.h>
#include <kernel/vm.h>
#include <vm/vm_aspace.h>
#include <lib/ktrace.h>
#include <magenta/device/intel-pt.h>
#include <magenta/ktrace.h>
#include <magenta/mtrace.h>
#include <magenta/thread_annotations.h>
#include <fbl/auto_lock.h>
#include <fbl/macros.h>
#include <fbl/mutex.h>
#include <fbl/unique_ptr.h>
#include <pow2.h>
#include <string.h>
#include <trace.h>

using fbl::AutoLock;

#define LOCAL_TRACE 0

// Control MSRs
#define IA32_RTIT_OUTPUT_BASE 0x560
#define IA32_RTIT_OUTPUT_MASK_PTRS 0x561
#define IA32_RTIT_CTL 0x570
#define IA32_RTIT_STATUS 0x571
#define IA32_RTIT_CR3_MATCH 0x572
#define IA32_RTIT_ADDR0_A 0x580
#define IA32_RTIT_ADDR0_B 0x581
#define IA32_RTIT_ADDR1_A 0x582
#define IA32_RTIT_ADDR1_B 0x583
#define IA32_RTIT_ADDR2_A 0x584
#define IA32_RTIT_ADDR2_B 0x585
#define IA32_RTIT_ADDR3_A 0x586
#define IA32_RTIT_ADDR3_B 0x587

// We need bits[15:8] to get the "maximum non-turbo ratio".
// See libipt:intel-pt.h:pt_config, and Intel Vol. 3 chapter 35.5.
#define IA32_PLATFORM_INFO 0xce

// Our own copy of what h/w supports, mostly for sanity checking.
static bool supports_pt = false;
static bool supports_cr3_filtering = false;
static bool supports_psb = false;
static bool supports_ip_filtering = false;
static bool supports_mtc = false;
static bool supports_ptwrite = false;
static bool supports_power_events = false;
static bool supports_output_topa = false;
static bool supports_output_topa_multi = false;
static bool supports_output_single = false;
static bool supports_output_transport = false;

struct ipt_cpu_state_t {
    uint64_t ctl;
    uint64_t status;
    uint64_t output_base;
    uint64_t output_mask_ptrs;
    uint64_t cr3_match;
    struct {
        uint64_t a,b;
    } addr_ranges[IPT_MAX_NUM_ADDR_RANGES];
};

static fbl::Mutex ipt_lock;

static ipt_cpu_state_t* ipt_cpu_state TA_GUARDED(ipt_lock);

static bool active TA_GUARDED(ipt_lock) = false;

static ipt_trace_mode_t trace_mode TA_GUARDED(ipt_lock) = IPT_TRACE_CPUS;

void x86_processor_trace_init(void)
{
    if (!x86_feature_test(X86_FEATURE_PT)) {
        return;
    }

    struct cpuid_leaf leaf;
    if (!x86_get_cpuid_subleaf(X86_CPUID_PT, 0, &leaf)) {
        return;
    }

    supports_pt = true;

    // Keep our own copy of these flags, mostly for potential sanity checks.
    supports_cr3_filtering = !!(leaf.b & (1<<0));
    supports_psb = !!(leaf.b & (1<<1));
    supports_ip_filtering = !!(leaf.b & (1<<2));
    supports_mtc = !!(leaf.b & (1<<3));
    supports_ptwrite = !!(leaf.b & (1<<4));
    supports_power_events = !!(leaf.b & (1<<5));

    supports_output_topa = !!(leaf.c & (1<<0));
    supports_output_topa_multi = !!(leaf.c & (1<<1));
    supports_output_single = !!(leaf.c & (1<<2));
    supports_output_transport = !!(leaf.c & (1<<3));
}

// Intel Processor Trace support needs to be able to map cr3 values that
// appear in the trace to pids that ld.so uses to dump memory maps.
void arch_trace_process_create(uint64_t pid, paddr_t pt_phys) {
    // The cr3 value that appears in Intel PT h/w tracing.
    uint64_t cr3 = pt_phys;
    ktrace(TAG_IPT_PROCESS_CREATE, (uint32_t)pid, (uint32_t)(pid >> 32),
           (uint32_t)cr3, (uint32_t)(cr3 >> 32));
}

// IPT tracing has two "modes":
// - per-cpu tracing
// - thread-specific tracing
// Tracing can only be done in one mode at a time. This is because saving/
// restoring thread PT state via the xsaves/xrstors instructions is a global
// flag in the XSS msr.

// Worker for x86_ipt_set_mode to be executed on all cpus.
// This is invoked via mp_sync_exec which thread safety analysis cannot follow.
static void x86_ipt_set_mode_task(void* raw_context) TA_NO_THREAD_SAFETY_ANALYSIS {
    DEBUG_ASSERT(arch_ints_disabled());
    DEBUG_ASSERT(!active);

    // When changing modes make sure all PT MSRs are in the init state.
    // We don't want a value to appear in the xsave buffer and have xrstors
    // #gp because XCOMP_BV has the PT bit set that's not set in XSS.
    // We still need to do this, even with MG-892, when transitioning
    // from IPT_TRACE_CPUS to IPT_TRACE_THREADS.
    write_msr(IA32_RTIT_CTL, 0);
    write_msr(IA32_RTIT_STATUS, 0);
    write_msr(IA32_RTIT_OUTPUT_BASE, 0);
    write_msr(IA32_RTIT_OUTPUT_MASK_PTRS, 0);
    if (supports_cr3_filtering)
        write_msr(IA32_RTIT_CR3_MATCH, 0);
    // TODO(dje): addr range msrs

    ipt_trace_mode_t new_mode = static_cast<ipt_trace_mode_t>(reinterpret_cast<uintptr_t>(raw_context));

    // PT state saving, if supported, was enabled during boot so there's no
    // need to recalculate the xsave space needed.
    x86_set_extended_register_pt_state(new_mode == IPT_TRACE_THREADS);
}

status_t x86_ipt_set_mode(ipt_trace_mode_t mode) {
    AutoLock al(&ipt_lock);

    if (!supports_pt)
        return MX_ERR_NOT_SUPPORTED;
    if (active)
        return MX_ERR_BAD_STATE;
    if (ipt_cpu_state)
        return MX_ERR_BAD_STATE;
    // Changing to the same mode is a no-op.
    // This check is still done after the above checks. E.g., it doesn't make
    // sense to call this function if tracing is active.
    if (mode == trace_mode)
        return MX_OK;

    // MG-892: We don't support changing the mode from IPT_TRACE_THREADS to
    // IPT_TRACE_CPUS: We can't turn off XSS.PT until we're sure all threads
    // have no PT state, and that's too tricky to do right now. Instead,
    // require the developer to reboot (the default is IPT_TRACE_CPUS).
    if (trace_mode == IPT_TRACE_THREADS && mode == IPT_TRACE_CPUS)
        return MX_ERR_NOT_SUPPORTED;

    mp_sync_exec(MP_IPI_TARGET_ALL, 0, x86_ipt_set_mode_task,
                 reinterpret_cast<void*>(static_cast<uintptr_t>(mode)));
    trace_mode = mode;

    return MX_OK;
}

// Allocate all needed state for tracing.

status_t x86_ipt_cpu_mode_alloc() {
    AutoLock al(&ipt_lock);

    if (!supports_pt)
        return MX_ERR_NOT_SUPPORTED;
    if (trace_mode == IPT_TRACE_THREADS)
        return MX_ERR_BAD_STATE;
    if (active)
        return MX_ERR_BAD_STATE;
    if (ipt_cpu_state)
        return MX_ERR_BAD_STATE;

    uint32_t num_cpus = arch_max_num_cpus();
    ipt_cpu_state =
        reinterpret_cast<ipt_cpu_state_t*>(calloc(num_cpus,
                                                  sizeof(*ipt_cpu_state)));
    if (!ipt_cpu_state)
        return MX_ERR_NO_MEMORY;
    return MX_OK;
}

// Free resources obtained by x86_ipt_cpu_mode_alloc().
// This doesn't care if resources have already been freed to save callers
// from having to care during any cleanup.

status_t x86_ipt_cpu_mode_free() {
    AutoLock al(&ipt_lock);

    if (!supports_pt)
        return MX_ERR_NOT_SUPPORTED;
    if (trace_mode == IPT_TRACE_THREADS)
        return MX_ERR_BAD_STATE;
    if (active)
        return MX_ERR_BAD_STATE;

    free(ipt_cpu_state);
    ipt_cpu_state = nullptr;
    return MX_OK;
}

// This is invoked via mp_sync_exec which thread safety analysis cannot follow.
static void x86_ipt_start_cpu_task(void* raw_context) TA_NO_THREAD_SAFETY_ANALYSIS {
    DEBUG_ASSERT(arch_ints_disabled());
    DEBUG_ASSERT(active && raw_context);

    ipt_cpu_state_t* context = reinterpret_cast<ipt_cpu_state_t*>(raw_context);
    uint32_t cpu = arch_curr_cpu_num();
    ipt_cpu_state_t* state = &context[cpu];

    DEBUG_ASSERT(!(read_msr(IA32_RTIT_CTL) & IPT_CTL_TRACE_EN_MASK));

    // Load the ToPA configuration
    write_msr(IA32_RTIT_OUTPUT_BASE, state->output_base);
    write_msr(IA32_RTIT_OUTPUT_MASK_PTRS, state->output_mask_ptrs);

    // Load all other msrs, prior to enabling tracing.
    write_msr(IA32_RTIT_STATUS, state->status);
    if (supports_cr3_filtering)
        write_msr(IA32_RTIT_CR3_MATCH, state->cr3_match);

    // Enable the trace
    write_msr(IA32_RTIT_CTL, state->ctl);
}

// Begin the trace.

status_t x86_ipt_cpu_mode_start() {
    AutoLock al(&ipt_lock);

    if (!supports_pt)
        return MX_ERR_NOT_SUPPORTED;
    if (trace_mode == IPT_TRACE_THREADS)
        return MX_ERR_BAD_STATE;
    if (active)
        return MX_ERR_BAD_STATE;
    if (!ipt_cpu_state)
        return MX_ERR_BAD_STATE;

    uint64_t kernel_cr3 = x86_kernel_cr3();
    TRACEF("Enabling processor trace, kernel cr3: 0x%" PRIxPTR "\n",
           kernel_cr3);

    active = true;

    uint64_t platform_msr = read_msr(IA32_PLATFORM_INFO);
    unsigned nom_freq = (platform_msr >> 8) & 0xff;
    ktrace(TAG_IPT_START, (uint32_t)nom_freq, 0,
           (uint32_t)kernel_cr3, (uint32_t)(kernel_cr3 >> 32));

    // Emit other sideband info needed by the trace reader.
    const struct x86_model_info* model_info = x86_get_model();
    ktrace(TAG_IPT_CPU_INFO, model_info->processor_type,
           model_info->display_family, model_info->display_model,
           model_info->stepping);

    mp_sync_exec(MP_IPI_TARGET_ALL, 0, x86_ipt_start_cpu_task, ipt_cpu_state);
    return MX_OK;
}

// This is invoked via mp_sync_exec which thread safety analysis cannot follow.
static void x86_ipt_stop_cpu_task(void* raw_context) TA_NO_THREAD_SAFETY_ANALYSIS {
    DEBUG_ASSERT(arch_ints_disabled());
    DEBUG_ASSERT(raw_context);

    ipt_cpu_state_t* context = reinterpret_cast<ipt_cpu_state_t*>(raw_context);
    uint32_t cpu = arch_curr_cpu_num();
    ipt_cpu_state_t* state = &context[cpu];

    // Disable the trace
    write_msr(IA32_RTIT_CTL, 0);

    // Retrieve msr values for later providing to userspace
    state->ctl = 0;
    state->status = read_msr(IA32_RTIT_STATUS);
    state->output_base = read_msr(IA32_RTIT_OUTPUT_BASE);
    state->output_mask_ptrs = read_msr(IA32_RTIT_OUTPUT_MASK_PTRS);

    // Zero all MSRs so that we are in the XSAVE initial configuration.
    // This allows h/w to do some optimizations regarding the state.
    write_msr(IA32_RTIT_STATUS, 0);
    write_msr(IA32_RTIT_OUTPUT_BASE, 0);
    write_msr(IA32_RTIT_OUTPUT_MASK_PTRS, 0);
    if (supports_cr3_filtering)
        write_msr(IA32_RTIT_CR3_MATCH, 0);

    // TODO(dje): Make it explicit that packets have been completely written.
    // See Intel Vol 3 chapter 36.2.4.

    // TODO(teisenbe): Clear ADDR* MSRs depending on leaf 1
}

// This can be called while not active, so the caller doesn't have to care
// during any cleanup.

status_t x86_ipt_cpu_mode_stop() {
    AutoLock al(&ipt_lock);

    if (!supports_pt)
        return MX_ERR_NOT_SUPPORTED;
    if (trace_mode == IPT_TRACE_THREADS)
        return MX_ERR_BAD_STATE;
    if (!ipt_cpu_state)
        return MX_ERR_BAD_STATE;

    TRACEF("Disabling processor trace\n");

    mp_sync_exec(MP_IPI_TARGET_ALL, 0, x86_ipt_stop_cpu_task, ipt_cpu_state);
    ktrace(TAG_IPT_STOP, 0, 0, 0, 0);
    active = false;
    return MX_OK;
}

status_t x86_ipt_stage_cpu_data(uint32_t cpu, const mx_x86_pt_regs_t* regs) {
    AutoLock al(&ipt_lock);

    if (!supports_pt)
        return MX_ERR_NOT_SUPPORTED;
    if (trace_mode == IPT_TRACE_THREADS)
        return MX_ERR_BAD_STATE;
    if (active)
        return MX_ERR_BAD_STATE;
    if (!ipt_cpu_state)
            return MX_ERR_BAD_STATE;
    uint32_t num_cpus = arch_max_num_cpus();
    if (cpu >= num_cpus)
        return MX_ERR_INVALID_ARGS;

    ipt_cpu_state[cpu].ctl = regs->ctl;
    ipt_cpu_state[cpu].status = regs->status;
    ipt_cpu_state[cpu].output_base = regs->output_base;
    ipt_cpu_state[cpu].output_mask_ptrs = regs->output_mask_ptrs;
    ipt_cpu_state[cpu].cr3_match = regs->cr3_match;
    static_assert(sizeof(ipt_cpu_state[cpu].addr_ranges) == sizeof(regs->addr_ranges), "addr_ranges size mismatch");
    memcpy(ipt_cpu_state[cpu].addr_ranges, regs->addr_ranges, sizeof(regs->addr_ranges));

    return MX_OK;
}

status_t x86_ipt_get_cpu_data(uint32_t cpu, mx_x86_pt_regs_t* regs) {
    AutoLock al(&ipt_lock);

    if (!supports_pt)
        return MX_ERR_NOT_SUPPORTED;
    if (trace_mode == IPT_TRACE_THREADS)
        return MX_ERR_BAD_STATE;
    if (active)
        return MX_ERR_BAD_STATE;
    if (!ipt_cpu_state)
        return MX_ERR_BAD_STATE;
    uint32_t num_cpus = arch_max_num_cpus();
    if (cpu >= num_cpus)
        return MX_ERR_INVALID_ARGS;

    regs->ctl = ipt_cpu_state[cpu].ctl;
    regs->status = ipt_cpu_state[cpu].status;
    regs->output_base = ipt_cpu_state[cpu].output_base;
    regs->output_mask_ptrs = ipt_cpu_state[cpu].output_mask_ptrs;
    regs->cr3_match = ipt_cpu_state[cpu].cr3_match;
    static_assert(sizeof(regs->addr_ranges) == sizeof(ipt_cpu_state[cpu].addr_ranges), "addr_ranges size mismatch");
    memcpy(regs->addr_ranges, ipt_cpu_state[cpu].addr_ranges, sizeof(regs->addr_ranges));

    return MX_OK;
}
