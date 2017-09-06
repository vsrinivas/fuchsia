// Copyright 2016 The Fuchsia Authors
// Copyright (c) 2009 Corey Tabaka
// Copyright (c) 2015 Intel Corporation
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <debug.h>
#include <trace.h>
#include <arch/exception.h>
#include <arch/user_copy.h>
#include <arch/x86.h>
#include <arch/x86/apic.h>
#include <arch/x86/feature.h>
#include <arch/x86/interrupts.h>
#include <arch/x86/descriptor.h>
#include <kernel/thread.h>
#include <kernel/stats.h>
#include <kernel/vm.h>
#include <platform.h>
#include <vm/fault.h>

#include <fbl/auto_call.h>
#include <magenta/syscalls/exception.h>

#include <lib/ktrace.h>


static void dump_fault_frame(x86_iframe_t *frame)
{
    dprintf(CRITICAL, " CS:  %#18" PRIx64 " RIP: %#18" PRIx64 " EFL: %#18" PRIx64 " CR2: %#18lx\n",
            frame->cs, frame->ip, frame->flags, x86_get_cr2());
    dprintf(CRITICAL, " RAX: %#18" PRIx64 " RBX: %#18" PRIx64 " RCX: %#18" PRIx64 " RDX: %#18" PRIx64 "\n",
            frame->rax, frame->rbx, frame->rcx, frame->rdx);
    dprintf(CRITICAL, " RSI: %#18" PRIx64 " RDI: %#18" PRIx64 " RBP: %#18" PRIx64 " RSP: %#18" PRIx64 "\n",
            frame->rsi, frame->rdi, frame->rbp, frame->user_sp);
    dprintf(CRITICAL, "  R8: %#18" PRIx64 "  R9: %#18" PRIx64 " R10: %#18" PRIx64 " R11: %#18" PRIx64 "\n",
            frame->r8, frame->r9, frame->r10, frame->r11);
    dprintf(CRITICAL, " R12: %#18" PRIx64 " R13: %#18" PRIx64 " R14: %#18" PRIx64 " R15: %#18" PRIx64 "\n",
            frame->r12, frame->r13, frame->r14, frame->r15);
    dprintf(CRITICAL, "errc: %#18" PRIx64 "\n",
            frame->err_code);

    // dump the bottom of the current stack
    void *stack = frame;

    if (frame->cs == CODE_64_SELECTOR) {
        dprintf(CRITICAL, "bottom of kernel stack at %p:\n", stack);
        hexdump(stack, 128);
    }
}

__NO_RETURN static void exception_die(x86_iframe_t *frame, const char *msg)
{
    platform_panic_start();

    printf("vector %lu\n", (ulong) frame->vector);
    dprintf(CRITICAL, "%s", msg);
    dump_fault_frame(frame);

    // try to dump the user stack
    if (is_user_address(frame->user_sp)) {
        uint8_t buf[256];
        if (arch_copy_from_user(buf, (void *)frame->user_sp, sizeof(buf)) == MX_OK) {
            printf("bottom of user stack at 0x%lx:\n", (vaddr_t)frame->user_sp);
            hexdump_ex(buf, sizeof(buf), frame->user_sp);
        }
    }

    platform_halt(HALT_ACTION_HALT, HALT_REASON_SW_PANIC);
}

static status_t call_dispatch_user_exception(uint kind,
                                             struct arch_exception_context *context,
                                             x86_iframe_t *frame)
{
    thread_t *thread = get_current_thread();
    x86_set_suspended_general_regs(&thread->arch, X86_GENERAL_REGS_IFRAME, frame);
    status_t status = dispatch_user_exception(kind, context);
    x86_reset_suspended_general_regs(&thread->arch);
    return status;
}

static bool try_dispatch_user_exception(x86_iframe_t *frame, uint kind)
{
    bool from_user = SELECTOR_PL(frame->cs) != 0;
    if (from_user) {
        struct arch_exception_context context = { false, frame, 0 };
        arch_set_in_int_handler(false);
        arch_enable_ints();
        status_t erc = call_dispatch_user_exception(kind, &context, frame);
        arch_disable_ints();
        arch_set_in_int_handler(true);
        if (erc == MX_OK)
            return true;
    }

    return false;
}

static void x86_debug_handler(x86_iframe_t *frame)
{
    if (try_dispatch_user_exception(frame, MX_EXCP_HW_BREAKPOINT))
        return;

    exception_die(frame, "unhandled hw breakpoint, halting\n");
}

static void x86_nmi_handler(x86_iframe_t *frame)
{
}

static void x86_breakpoint_handler(x86_iframe_t *frame)
{
    if (try_dispatch_user_exception(frame, MX_EXCP_SW_BREAKPOINT))
        return;

    exception_die(frame, "unhandled sw breakpoint, halting\n");
}

static void x86_gpf_handler(x86_iframe_t *frame)
{
    DEBUG_ASSERT(arch_ints_disabled());

    // Check if we were doing a GPF test, e.g. to check if an MSR exists.
    struct x86_percpu *percpu = x86_get_percpu();
    if (unlikely(percpu->gpf_return_target)) {
        ASSERT(SELECTOR_PL(frame->cs) == 0);

        // Set up return to new address
        frame->ip = percpu->gpf_return_target;
        percpu->gpf_return_target = 0;
        return;
    }

    if (try_dispatch_user_exception(frame, MX_EXCP_GENERAL))
        return;

    exception_die(frame, "unhandled gpf, halting\n");
}

static void x86_invop_handler(x86_iframe_t *frame)
{
    if (try_dispatch_user_exception(frame, MX_EXCP_UNDEFINED_INSTRUCTION))
        return;

    exception_die(frame, "invalid opcode, halting\n");
}

static void x86_df_handler(x86_iframe_t *frame)
{
    // Do not give the user exception handler the opportunity to handle double
    // faults, since they indicate an unexpected system state and cannot be
    // recovered from.
    exception_die(frame, "double fault, halting\n");
}

static void x86_unhandled_exception(x86_iframe_t *frame)
{
    if (try_dispatch_user_exception(frame, MX_EXCP_GENERAL))
        return;

    exception_die(frame, "unhandled exception, halting\n");
}

static void x86_dump_pfe(x86_iframe_t *frame, ulong cr2)
{
    uint64_t error_code = frame->err_code;

    addr_t v_addr = cr2;
    addr_t ssp = frame->user_ss & X86_8BYTE_MASK;
    addr_t sp = frame->user_sp;
    addr_t cs  = frame->cs & X86_8BYTE_MASK;
    addr_t ip = frame->ip;

    dprintf(CRITICAL, "<PAGE FAULT> Instruction Pointer   = 0x%lx:0x%lx\n",
            (ulong)cs,
            (ulong)ip);
    dprintf(CRITICAL, "<PAGE FAULT> Stack Pointer         = 0x%lx:0x%lx\n",
            (ulong)ssp,
            (ulong)sp);
    dprintf(CRITICAL, "<PAGE FAULT> Fault Linear Address  = 0x%lx\n",
            (ulong)v_addr);
    dprintf(CRITICAL, "<PAGE FAULT> Error Code Value      = 0x%lx\n",
            (ulong)error_code);
    dprintf(CRITICAL, "<PAGE FAULT> Error Code Type       = %s %s %s%s, %s\n",
            error_code & PFEX_U ? "user" : "supervisor",
            error_code & PFEX_W ? "write" : "read",
            error_code & PFEX_I ? "instruction" : "data",
            error_code & PFEX_RSV ? " rsv" : "",
            error_code & PFEX_P ? "protection violation" : "page not present");
}


__NO_RETURN static void x86_fatal_pfe_handler(x86_iframe_t *frame, ulong cr2)
{
    x86_dump_pfe(frame, cr2);

    uint64_t error_code = frame->err_code;

    dump_thread(get_current_thread(), true);

    if (error_code & PFEX_U) {
        // User mode page fault
        switch (error_code) {
            case 4:
            case 5:
            case 6:
            case 7:
                exception_die(frame, "User Page Fault exception, halting\n");
                break;
        }
    } else {
        // Supervisor mode page fault
        switch (error_code) {

            case 0:
            case 1:
            case 2:
            case 3:
                exception_die(frame, "Supervisor Page Fault exception, halting\n");
                break;
        }
    }

    exception_die(frame, "unhandled page fault, halting\n");
}

static status_t x86_pfe_handler(x86_iframe_t *frame)
{
    /* Handle a page fault exception */
    uint64_t error_code = frame->err_code;
    vaddr_t va = x86_get_cr2();

    /* reenable interrupts */
    arch_set_in_int_handler(false);
    arch_enable_ints();

    /* make sure we put interrupts back as we exit */
    auto ac = fbl::MakeAutoCall([]() {
        arch_disable_ints();
        arch_set_in_int_handler(true);
    });

    /* check for flags we're not prepared to handle */
    if (unlikely(error_code & ~(PFEX_I | PFEX_U | PFEX_W | PFEX_P))) {
        printf("x86_pfe_handler: unhandled error code bits set, error code %#" PRIx64 "\n", error_code);
        return MX_ERR_NOT_SUPPORTED;
    }

    /* check for a potential SMAP failure */
    if (unlikely(
        !(error_code & PFEX_U) &&
         (error_code & PFEX_P) &&
        x86_feature_test(X86_FEATURE_SMAP) &&
        !(frame->flags & X86_FLAGS_AC) &&
         is_user_address(va))) {
        /* supervisor mode page-present access failure with the AC bit clear (SMAP enabled) */
        printf("x86_pfe_handler: potential SMAP failure, supervisor access at address %#" PRIxPTR "\n", va);
        return MX_ERR_ACCESS_DENIED;
    }

    /* convert the PF error codes to page fault flags */
    uint flags = 0;
    flags |= (error_code & PFEX_W) ? VMM_PF_FLAG_WRITE : 0;
    flags |= (error_code & PFEX_U) ? VMM_PF_FLAG_USER : 0;
    flags |= (error_code & PFEX_I) ? VMM_PF_FLAG_INSTRUCTION : 0;
    flags |= (error_code & PFEX_P) ? 0 : VMM_PF_FLAG_NOT_PRESENT;

    /* call the high level page fault handler */
    status_t pf_err = vmm_page_fault_handler(va, flags);
    if (likely(pf_err == MX_OK))
        return MX_OK;

    /* if the high level page fault handler can't deal with it,
     * resort to trying to recover first, before bailing */

    /* Check if a resume address is specified, and just return to it if so */
    thread_t *current_thread = get_current_thread();
    if (unlikely(current_thread->arch.page_fault_resume)) {
        frame->ip = (uintptr_t)current_thread->arch.page_fault_resume;
        return MX_OK;
    }

    /* let high level code deal with this */
    bool from_user = SELECTOR_PL(frame->cs) != 0;
    if (from_user) {
        CPU_STATS_INC(exceptions);
        struct arch_exception_context context = { true, frame, va };
        return call_dispatch_user_exception(MX_EXCP_FATAL_PAGE_FAULT,
                                            &context, frame);
    }

    /* fall through to fatal path */
    return MX_ERR_NOT_SUPPORTED;
}

static void x86_iframe_process_pending_signals(x86_iframe_t *frame)
{
    thread_t *thread = get_current_thread();
    if (unlikely(thread_is_signaled(thread))) {
        x86_set_suspended_general_regs(&thread->arch, X86_GENERAL_REGS_IFRAME, frame);
        thread_process_pending_signals();
        x86_reset_suspended_general_regs(&thread->arch);
    }
}

/* top level x86 exception handler for most exceptions and irqs */
void x86_exception_handler(x86_iframe_t *frame)
{
    // are we recursing?
    if (unlikely(arch_in_int_handler()) && frame->vector != X86_INT_NMI) {
        exception_die(frame, "recursion in interrupt handler\n");
    }

    arch_set_in_int_handler(true);

    // did we come from user or kernel space?
    bool from_user = SELECTOR_PL(frame->cs) != 0;

    // deliver the interrupt
    enum handler_return ret = INT_NO_RESCHEDULE;

    ktrace_tiny(TAG_IRQ_ENTER, ((uint32_t)frame->vector << 8) | arch_curr_cpu_num());

    switch (frame->vector) {
        case X86_INT_DEBUG:
            CPU_STATS_INC(exceptions);
            x86_debug_handler(frame);
            break;
        case X86_INT_NMI:
            CPU_STATS_INC(exceptions);
            x86_nmi_handler(frame);
            break;
        case X86_INT_BREAKPOINT:
            CPU_STATS_INC(exceptions);
            x86_breakpoint_handler(frame);
            break;

        case X86_INT_INVALID_OP:
            CPU_STATS_INC(exceptions);
            x86_invop_handler(frame);
            break;

        case X86_INT_DEVICE_NA:
            CPU_STATS_INC(exceptions);
            exception_die(frame, "device na fault\n");
            break;

        case X86_INT_DOUBLE_FAULT:
            x86_df_handler(frame);
            break;
        case X86_INT_FPU_FP_ERROR: {
            CPU_STATS_INC(exceptions);
            uint16_t fsw;
            __asm__ __volatile__("fnstsw %0" : "=m" (fsw));
            TRACEF("fsw 0x%hx\n", fsw);
            exception_die(frame, "x87 math fault\n");
            break;
        }
        case X86_INT_SIMD_FP_ERROR: {
            CPU_STATS_INC(exceptions);
            uint32_t mxcsr;
            __asm__ __volatile__("stmxcsr %0" : "=m" (mxcsr));
            TRACEF("mxcsr 0x%x\n", mxcsr);
            exception_die(frame, "simd math fault\n");
            break;
        }
        case X86_INT_GP_FAULT:
            CPU_STATS_INC(exceptions);
            x86_gpf_handler(frame);
            break;

        case X86_INT_PAGE_FAULT:
            CPU_STATS_INC(page_faults);
            if (x86_pfe_handler(frame) != MX_OK)
                x86_fatal_pfe_handler(frame, x86_get_cr2());
            break;

        /* ignore spurious APIC irqs */
        case X86_INT_APIC_SPURIOUS:
            break;
        case X86_INT_APIC_ERROR: {
            ret = apic_error_interrupt_handler();
            apic_issue_eoi();
            break;
        }
        case X86_INT_APIC_TIMER: {
            ret = apic_timer_interrupt_handler();
            apic_issue_eoi();
            break;
        }
        case X86_INT_IPI_GENERIC: {
            ret = x86_ipi_generic_handler();
            apic_issue_eoi();
            break;
        }
        case X86_INT_IPI_RESCHEDULE: {
            ret = x86_ipi_reschedule_handler();
            apic_issue_eoi();
            break;
        }
        case X86_INT_IPI_HALT: {
            x86_ipi_halt_handler();
            /* no return */
            break;
        }
        /* pass all other non-Intel defined irq vectors to the platform */
        case X86_INT_PLATFORM_BASE  ... X86_INT_PLATFORM_MAX: {
            CPU_STATS_INC(interrupts);
            ret = platform_irq(frame);
            break;
        }
        default:
            x86_unhandled_exception(frame);
            break;
    }

    /* at this point we're able to be rescheduled, so we're 'outside' of the int handler */
    arch_set_in_int_handler(false);

    /* if we came from user space, check to see if we have any signals to handle */
    if (unlikely(from_user)) {
        /* in the case of receiving a kill signal, this function may not return,
         * but the scheduler would have been invoked so it's fine.
         */
        x86_iframe_process_pending_signals(frame);
    }

    if (ret != INT_NO_RESCHEDULE)
        thread_preempt();

    ktrace_tiny(TAG_IRQ_EXIT, ((uint)frame->vector << 8) | arch_curr_cpu_num());

    DEBUG_ASSERT_MSG(arch_ints_disabled(),
        "ints disabled on way out of exception, vector %" PRIu64 " IP %#" PRIx64 "\n",
        frame->vector, frame->ip);
}

__WEAK x86_64_syscall_result unknown_syscall(uint64_t syscall_num, uint64_t ip)
{
    PANIC_UNIMPLEMENTED;
}

void x86_syscall_process_pending_signals(x86_syscall_general_regs_t *gregs)
{
    thread_t *thread = get_current_thread();
    x86_set_suspended_general_regs(&thread->arch, X86_GENERAL_REGS_SYSCALL, gregs);
    thread_process_pending_signals();
    x86_reset_suspended_general_regs(&thread->arch);
}

void arch_dump_exception_context(const arch_exception_context_t *context)
{
    if (context->is_page_fault) {
        x86_dump_pfe(context->frame, context->cr2);
    }

    dump_fault_frame(context->frame);

    // try to dump the user stack
    if (context->frame->cs != CODE_64_SELECTOR && is_user_address(context->frame->user_sp)) {
        uint8_t buf[256];
        if (arch_copy_from_user(buf, (void *)context->frame->user_sp, sizeof(buf)) == MX_OK) {
            printf("bottom of user stack at 0x%lx:\n", (vaddr_t)context->frame->user_sp);
            hexdump_ex(buf, sizeof(buf), context->frame->user_sp);
        }
    }
}

void arch_fill_in_exception_context(const arch_exception_context_t *arch_context, mx_exception_report_t *report)
{
    mx_exception_context_t *mx_context = &report->context;

    mx_context->arch.u.x86_64.vector = arch_context->frame->vector;
    mx_context->arch.u.x86_64.err_code = arch_context->frame->err_code;
    mx_context->arch.u.x86_64.cr2 = arch_context->cr2;
}

status_t arch_dispatch_user_policy_exception(void)
{
    x86_iframe_t frame = {};
    arch_exception_context_t context = {};
    context.frame = &frame;
    return dispatch_user_exception(MX_EXCP_POLICY_ERROR, &context);
}
