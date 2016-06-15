// Copyright 2016 The Fuchsia Authors
// Copyright (c) 2009 Corey Tabaka
// Copyright (c) 2015 Intel Corporation
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <debug.h>
#include <trace.h>
#include <arch/x86.h>
#include <arch/x86/apic.h>
#include <arch/x86/interrupts.h>
#include <arch/x86/descriptor.h>
#include <arch/fpu.h>
#include <kernel/thread.h>

#include <lib/user_copy.h>

#if WITH_LIB_MAGENTA
#include <magenta/exception.h>

struct arch_exception_context {
    bool is_page_fault;
    x86_iframe_t *frame;
    ulong cr2;
};
#endif

extern enum handler_return platform_irq(x86_iframe_t *frame);

static void dump_fault_frame(x86_iframe_t *frame)
{
#if ARCH_X86_32
    dprintf(CRITICAL, " CS:     %04x EIP: %08x EFL: %08x CR2: %08lx\n",
            frame->cs, frame->ip, frame->flags, x86_get_cr2());
    dprintf(CRITICAL, "EAX: %08x ECX: %08x EDX: %08x EBX: %08x\n",
            frame->eax, frame->ecx, frame->edx, frame->ebx);
    dprintf(CRITICAL, "ESP: %08x EBP: %08x ESI: %08x EDI: %08x\n",
            frame->esp, frame->ebp, frame->esi, frame->edi);
    dprintf(CRITICAL, " DS:     %04x  ES:     %04x  FS:   %04x  GS:     %04x\n",
            frame->ds, frame->es, frame->fs, frame->gs);
#elif ARCH_X86_64
    dprintf(CRITICAL, " CS:              %4llx RIP: %16llx EFL: %16llx CR2: %16lx\n",
            frame->cs, frame->ip, frame->flags, x86_get_cr2());
    dprintf(CRITICAL, " RAX: %16llx RBX: %16llx RCX: %16llx RDX: %16llx\n",
            frame->rax, frame->rbx, frame->rcx, frame->rdx);
    dprintf(CRITICAL, " RSI: %16llx RDI: %16llx RBP: %16llx RSP: %16llx\n",
            frame->rsi, frame->rdi, frame->rbp, frame->user_sp);
    dprintf(CRITICAL, "  R8: %16llx  R9: %16llx R10: %16llx R11: %16llx\n",
            frame->r8, frame->r9, frame->r10, frame->r11);
    dprintf(CRITICAL, " R12: %16llx R13: %16llx R14: %16llx R15: %16llx\n",
            frame->r12, frame->r13, frame->r14, frame->r15);
    dprintf(CRITICAL, "errc: %16llx\n",
            frame->err_code);
#endif

    // dump the bottom of the current stack
    void *stack = frame;

    if (frame->cs == CODE_64_SELECTOR) {
        dprintf(CRITICAL, "bottom of kernel stack at %p:\n", stack);
        hexdump(stack, 128);
    }
}

static void exception_die(x86_iframe_t *frame, const char *msg)
{
    dprintf(CRITICAL, "%s", msg);
    dump_fault_frame(frame);

#if ARCH_X86_64
    // try to dump the user stack
    if (is_user_address(frame->user_sp)) {
        uint8_t buf[256];
        if (copy_from_user(buf, (void *)frame->user_sp, sizeof(buf)) == NO_ERROR) {
            printf("bottom of user stack at 0x%lx:\n", (vaddr_t)frame->user_sp);
            hexdump_ex(buf, sizeof(buf), frame->user_sp);
        }
    }
#endif

    for (;;) {
        x86_cli();
        x86_hlt();
    }
}

void x86_syscall_handler(x86_iframe_t *frame)
{
    exception_die(frame, "unhandled syscall, halting\n");
}

void x86_gpf_handler(x86_iframe_t *frame)
{
#if WITH_LIB_MAGENTA
    struct arch_exception_context context = { .frame = frame, .is_page_fault = false };
    arch_enable_ints();
    status_t erc = magenta_exception_handler(EXC_GENERAL, &context, frame->ip);
    arch_disable_ints();
    if (erc == NO_ERROR)
        return;
#endif
    exception_die(frame, "unhandled gpf, halting\n");
}

void x86_invop_handler(x86_iframe_t *frame)
{
#if WITH_LIB_MAGENTA
    struct arch_exception_context context = { .frame = frame, .is_page_fault = false };
    arch_enable_ints();
    status_t erc = magenta_exception_handler(EXC_UNDEFINED_INSTRUCTION, &context, frame->ip);
    arch_disable_ints();
    if (erc == NO_ERROR)
        return;
#endif
    exception_die(frame, "invalid opcode, halting\n");
}

void x86_unhandled_exception(x86_iframe_t *frame)
{
#if WITH_LIB_MAGENTA
    struct arch_exception_context context = { .frame = frame, .is_page_fault = false };
    arch_enable_ints();
    status_t erc = magenta_exception_handler(EXC_GENERAL, &context, frame->ip);
    arch_disable_ints();
    if (erc == NO_ERROR)
        return;
#endif
    printf("vector %u\n", (uint)frame->vector);
    exception_die(frame, "unhandled exception, halting\n");
}

static void x86_dump_pfe(x86_iframe_t *frame, ulong cr2)
{
    uint32_t error_code = frame->err_code;

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


static void x86_fatal_pfe_handler(x86_iframe_t *frame, ulong cr2)
{
    x86_dump_pfe(frame, cr2);

    uint32_t error_code = frame->err_code;

    dump_thread(get_current_thread());

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

void x86_pfe_handler(x86_iframe_t *frame)
{
    /* Handle a page fault exception */
    uint32_t error_code = frame->err_code;
    vaddr_t va = x86_get_cr2();

    /* reenable interrupts */
    arch_enable_ints();

    /* check for flags we're not prepared to handle */
    if (unlikely(error_code & ~(PFEX_I | PFEX_U | PFEX_W | PFEX_P))) {
        printf("x86_pfe_handler: unhandled error code bits set, error code 0x%x\n", error_code);
        x86_fatal_pfe_handler(frame, va);
    }

    /* convert the PF error codes to page fault flags */
    uint flags = 0;
    flags |= (error_code & PFEX_W) ? VMM_PF_FLAG_WRITE : 0;
    flags |= (error_code & PFEX_U) ? VMM_PF_FLAG_USER : 0;
    flags |= (error_code & PFEX_I) ? VMM_PF_FLAG_INSTRUCTION : 0;

    int pf_err = vmm_page_fault_handler(va, flags);
    if (unlikely(pf_err < 0)) {
        /* if the high level page fault handler can't deal with it,
         * resort to trying to recover first, before bailing */

#ifdef ARCH_X86_64
        /* Check if a resume address is specified, and just return to it if so */
        thread_t *current_thread = get_current_thread();
        if (unlikely(current_thread->arch.page_fault_resume)) {
            frame->ip = (uintptr_t)current_thread->arch.page_fault_resume;
            return;
        }
#endif

        /* let high level code deal with this */
#if WITH_LIB_MAGENTA
        struct arch_exception_context context = { .frame = frame, .is_page_fault = true, .cr2 = va };
        status_t erc = magenta_exception_handler(EXC_FATAL_PAGE_FAULT, &context, frame->ip);
        arch_disable_ints();
        if (erc == NO_ERROR)
            return;
#else
        arch_disable_ints();
#endif


        /* fatal (for now) */
        x86_fatal_pfe_handler(frame, va);
    }
}

/* top level x86 exception handler for most exceptions and irqs */
void x86_exception_handler(x86_iframe_t *frame)
{
    THREAD_STATS_INC(interrupts);

    // deliver the interrupt
    enum handler_return ret = INT_NO_RESCHEDULE;

    switch (frame->vector) {
        case X86_INT_INVALID_OP:
            x86_invop_handler(frame);
            break;

        case X86_INT_DEVICE_NA: {
            // did we come from user or kernel space?
            bool from_user = SELECTOR_PL(frame->cs) != 0;
            if (unlikely(!from_user)) {
                exception_die(frame, "invalid fpu use in kernel\n");
            }
            fpu_dev_na_handler();
            break;
        }

        case X86_INT_FPU_FP_ERROR: {
            uint16_t fsw;
            __asm__ __volatile__("fnstsw %0" : "=m" (fsw));
            TRACEF("fsw 0x%hx\n", fsw);
            exception_die(frame, "x87 math fault\n");
            //asm volatile("fnclex");
            break;
        }
        case X86_INT_SIMD_FP_ERROR: {
            uint32_t mxcsr;
            __asm__ __volatile__("stmxcsr %0" : "=m" (mxcsr));
            TRACEF("mxcsr 0x%x\n", mxcsr);
            exception_die(frame, "simd math fault\n");
            break;
        }
        case X86_INT_GP_FAULT:
            x86_gpf_handler(frame);
            break;

        case X86_INT_PAGE_FAULT:
            x86_pfe_handler(frame);
            break;

        /* ignore spurious APIC irqs */
        case X86_INT_APIC_SPURIOUS: break;
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
#if WITH_SMP
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
#endif
        /* pass all other non-Intel defined irq vectors to the platform */
        case X86_INT_PLATFORM_BASE  ... X86_INT_PLATFORM_MAX: {
            ret = platform_irq(frame);
            break;
        }
        default:
            x86_unhandled_exception(frame);
    }

    if (ret != INT_NO_RESCHEDULE)
        thread_preempt();
}

__WEAK uint64_t x86_64_syscall(uint64_t arg1, uint64_t arg2, uint64_t arg3, uint64_t arg4,
                               uint64_t arg5, uint64_t arg6, uint64_t arg7, uint64_t arg8,
                               uint64_t syscall_num)
{
    PANIC_UNIMPLEMENTED;
}

#if WITH_LIB_MAGENTA
void arch_dump_exception_context(arch_exception_context_t *context)
{
    if (context->is_page_fault) {
        x86_dump_pfe(context->frame, context->cr2);
    }

    dump_fault_frame(context->frame);

    // try to dump the user stack
    if (context->frame->cs != CODE_64_SELECTOR && is_user_address(context->frame->user_sp)) {
        uint8_t buf[256];
        if (copy_from_user(buf, (void *)context->frame->user_sp, sizeof(buf)) == NO_ERROR) {
            printf("bottom of user stack at 0x%lx:\n", (vaddr_t)context->frame->user_sp);
            hexdump_ex(buf, sizeof(buf), context->frame->user_sp);
        }
    }
}
#endif


