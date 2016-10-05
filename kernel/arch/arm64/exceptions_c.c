// Copyright 2016 The Fuchsia Authors
// Copyright (c) 2014 Travis Geiselbrecht
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <stdio.h>
#include <debug.h>
#include <bits.h>
#include <inttypes.h>
#include <trace.h>
#include <arch/arch_ops.h>
#include <arch/arm64.h>
#include <kernel/thread.h>
#include <platform.h>

#if WITH_LIB_MAGENTA
#include <lib/user_copy.h>
#include <magenta/exception.h>
#endif

#define LOCAL_TRACE 0

struct fault_handler_table_entry {
    uint64_t pc;
    uint64_t fault_handler;
};

extern struct fault_handler_table_entry __fault_handler_table_start[];
extern struct fault_handler_table_entry __fault_handler_table_end[];

bool arm64_in_int_handler[SMP_MAX_CPUS];

static void dump_iframe(const struct arm64_iframe_long *iframe)
{
    printf("iframe %p:\n", iframe);
    printf("x0  %#18" PRIx64 " x1  %#18" PRIx64 " x2  %#18" PRIx64 " x3  %#18" PRIx64 "\n", iframe->r[0], iframe->r[1], iframe->r[2], iframe->r[3]);
    printf("x4  %#18" PRIx64 " x5  %#18" PRIx64 " x6  %#18" PRIx64 " x7  %#18" PRIx64 "\n", iframe->r[4], iframe->r[5], iframe->r[6], iframe->r[7]);
    printf("x8  %#18" PRIx64 " x9  %#18" PRIx64 " x10 %#18" PRIx64 " x11 %#18" PRIx64 "\n", iframe->r[8], iframe->r[9], iframe->r[10], iframe->r[11]);
    printf("x12 %#18" PRIx64 " x13 %#18" PRIx64 " x14 %#18" PRIx64 " x15 %#18" PRIx64 "\n", iframe->r[12], iframe->r[13], iframe->r[14], iframe->r[15]);
    printf("x16 %#18" PRIx64 " x17 %#18" PRIx64 " x18 %#18" PRIx64 " x19 %#18" PRIx64 "\n", iframe->r[18], iframe->r[17], iframe->r[18], iframe->r[19]);
    printf("x20 %#18" PRIx64 " x21 %#18" PRIx64 " x22 %#18" PRIx64 " x23 %#18" PRIx64 "\n", iframe->r[20], iframe->r[21], iframe->r[22], iframe->r[23]);
    printf("x24 %#18" PRIx64 " x25 %#18" PRIx64 " x26 %#18" PRIx64 " x27 %#18" PRIx64 "\n", iframe->r[24], iframe->r[25], iframe->r[26], iframe->r[27]);
    printf("x28 %#18" PRIx64 " x29 %#18" PRIx64 " lr  %#18" PRIx64 " usp %#18" PRIx64 "\n", iframe->r[28], iframe->r[29], iframe->lr, iframe->usp);
    printf("elr  %#18" PRIx64 "\n", iframe->elr);
    printf("spsr %#18" PRIx64 "\n", iframe->spsr);
}

__WEAK void arm64_syscall(struct arm64_iframe_long *iframe, bool is_64bit, uint32_t syscall_imm, uint64_t pc)
{
    panic("unhandled syscall vector\n");
}

void arm64_sync_exception(struct arm64_iframe_long *iframe, uint exception_flags)
{
    struct fault_handler_table_entry *fault_handler;
    uint32_t esr = ARM64_READ_SYSREG(esr_el1);
    uint32_t ec = BITS_SHIFT(esr, 31, 26);
    uint32_t il = BIT(esr, 25);
    uint32_t iss = BITS(esr, 24, 0);

    switch (ec) {
        case 0b000000: /* unknown reason */
            /* this is for a lot of reasons, but most of them are undefined instructions */
        case 0b111000: /* BRK from arm32 */
        case 0b111100: { /* BRK from arm64 */
#if WITH_LIB_MAGENTA
            /* let magenta get a shot at it */
            arch_exception_context_t context = { .frame = iframe, .esr = esr };
            arch_enable_ints();
            status_t erc = magenta_exception_handler(MX_EXCP_UNDEFINED_INSTRUCTION, &context, iframe->elr);
            arch_disable_ints();
            if (erc == NO_ERROR)
                return;
#endif
            return;
        }
        case 0b000111: /* floating point */
            if (unlikely((exception_flags & ARM64_EXCEPTION_FLAG_LOWER_EL) == 0)) {
                /* we trapped a floating point instruction inside our own EL, this is bad */
                printf("invalid fpu use in kernel: PC at %#" PRIx64 "\n",
                       iframe->elr);
                break;
            }
            arm64_fpu_exception(iframe, exception_flags);
            return;
        case 0b010001: /* syscall from arm32 */
        case 0b010101: /* syscall from arm64 */
#ifdef WITH_LIB_SYSCALL
            void arm64_syscall(struct arm64_iframe_long *iframe);
            arch_enable_fiqs();
            arm64_syscall(iframe);
            arch_disable_fiqs();
            return;
#else
            arm64_syscall(iframe, (ec == 0x15) ? true : false, iss & 0xffff, iframe->elr);
            return;
#endif
        case 0b100000: /* instruction abort from lower level */
        case 0b100001: { /* instruction abort from same level */
            /* read the FAR register */
            uint64_t far = ARM64_READ_SYSREG(far_el1);
            bool is_user = !BIT(ec, 0);

            uint pf_flags = VMM_PF_FLAG_INSTRUCTION;
            pf_flags |= is_user ? VMM_PF_FLAG_USER : 0;
            /* Check if this was not permission fault */
            if ((iss & 0b111100) != 0b001100) {
                pf_flags |= VMM_PF_FLAG_NOT_PRESENT;
            }

            LTRACEF("instruction abort: PC at %#" PRIx64
                    ", is_user %d, FAR %" PRIx64 ", esr 0x%x, iss 0x%x\n",
                    iframe->elr, is_user, far, esr, iss);

            arch_enable_ints();
            status_t err = vmm_page_fault_handler(far, pf_flags);
            arch_disable_ints();
            if (err >= 0)
                return;

#if WITH_LIB_MAGENTA
            /* if this is from user space, let magenta get a shot at it */
            if (is_user) {
                arch_exception_context_t context = { .frame = iframe, .esr = esr, .far = far };
                arch_enable_ints();
                status_t erc = magenta_exception_handler(MX_EXCP_FATAL_PAGE_FAULT, &context, iframe->elr);
                arch_disable_ints();
                if (erc == NO_ERROR)
                    return;
            }
#endif

            printf("instruction abort: PC at %#" PRIx64 "\n", iframe->elr);
            break;
        }
        case 0b100100: /* data abort from lower level */
        case 0b100101: { /* data abort from same level */
            /* read the FAR register */
            uint64_t far = ARM64_READ_SYSREG(far_el1);
            bool is_user = !BIT(ec, 0);

            uint pf_flags = 0;
            pf_flags |= BIT(iss, 6) ? VMM_PF_FLAG_WRITE : 0;
            pf_flags |= is_user ? VMM_PF_FLAG_USER : 0;
            /* Check if this was not permission fault */
            if ((iss & 0b111100) != 0b001100) {
                pf_flags |= VMM_PF_FLAG_NOT_PRESENT;
            }

            LTRACEF("data fault: PC at %#" PRIx64
                    ", is_user %d, FAR %#" PRIx64 ", esr 0x%x, iss 0x%x\n",
                    iframe->elr, is_user, far, esr, iss);

            arch_enable_ints();
            status_t err = vmm_page_fault_handler(far, pf_flags);
            arch_disable_ints();
            if (err >= 0)
                return;

            // Check if the current thread was expecting a data fault and
            // we should return to its handler.
            thread_t *thr = get_current_thread();
            if (thr->arch.data_fault_resume != NULL) {
                iframe->elr = (uintptr_t)thr->arch.data_fault_resume;
                return;
            }

            for (fault_handler = __fault_handler_table_start;
                    fault_handler < __fault_handler_table_end;
                    fault_handler++) {
                if (fault_handler->pc == iframe->elr) {
                    iframe->elr = fault_handler->fault_handler;
                    return;
                }
            }

#if WITH_LIB_MAGENTA
            /* if this is from user space, let magenta get a shot at it */
            if (is_user) {
                arch_exception_context_t context = { .frame = iframe, .esr = esr, .far = far };
                arch_enable_ints();
                status_t erc = magenta_exception_handler(MX_EXCP_FATAL_PAGE_FAULT, &context, iframe->elr);
                arch_disable_ints();
                if (erc == NO_ERROR)
                    return;
            }
#endif

            /* decode the iss */
            if (BIT(iss, 24)) { /* ISV bit */
                printf("data fault: PC at %#" PRIx64
                       ", FAR %#" PRIx64 ", iss %#x (DFSC %#x)\n",
                       iframe->elr, far, iss, BITS(iss, 5, 0));
            } else {
                printf("data fault: PC at %#" PRIx64
                       ", FAR %#" PRIx64 ", iss 0x%x\n",
                       iframe->elr, far, iss);
            }

            break;
        }
        default: {
#if WITH_LIB_MAGENTA
            /* TODO: properly decode more of these, since they may be originating in kernel space */
            /* let magenta get a shot at it */
            arch_exception_context_t context = { .frame = iframe, .esr = esr };
            arch_enable_ints();
            status_t erc = magenta_exception_handler(MX_EXCP_GENERAL, &context, iframe->elr);
            arch_disable_ints();
            if (erc == NO_ERROR)
                return;
#endif
            printf("unhandled synchronous exception\n");
        }
    }

    /* fatal exception, die here */
    printf("ESR 0x%x: ec 0x%x, il 0x%x, iss 0x%x\n", esr, ec, il, iss);
    dump_iframe(iframe);

    platform_halt(HALT_ACTION_HALT, HALT_REASON_SW_PANIC);
}

/* called from assembly */
void arm64_irq(struct arm64_iframe_short *iframe, uint exception_flags);

void arm64_irq(struct arm64_iframe_short *iframe, uint exception_flags)
{
    LTRACEF("iframe %p, flags 0x%x\n", iframe, exception_flags);

    uint32_t curr_cpu = arch_curr_cpu_num();
    arm64_in_int_handler[curr_cpu] = true;

    enum handler_return ret = platform_irq(iframe);

    arm64_in_int_handler[curr_cpu] = false;

    /* if we came from user space, check to see if we have any signals to handle */
    if (unlikely(exception_flags & ARM64_EXCEPTION_FLAG_LOWER_EL)) {
        /* in the case of receiving a kill signal, this function may not return,
         * but the scheduler would have been invoked so it's fine.
         */
        thread_process_pending_signals();
    }

    /* preempt the thread if the interrupt has signaled it */
    if (ret != INT_NO_RESCHEDULE)
        thread_preempt(true);
}

/* called from assembly */
void arm64_invalid_exception(struct arm64_iframe_long *iframe, unsigned int which);

void arm64_invalid_exception(struct arm64_iframe_long *iframe, unsigned int which)
{
    printf("invalid exception, which 0x%x\n", which);
    dump_iframe(iframe);

    platform_halt(HALT_ACTION_HALT, HALT_REASON_SW_PANIC);
}

#if WITH_LIB_MAGENTA
void arch_dump_exception_context(const arch_exception_context_t *context)
{
    uint32_t ec = BITS_SHIFT(context->esr, 31, 26);
    uint32_t iss = BITS(context->esr, 24, 0);

    switch (ec) {
        case 0b100000: /* instruction abort from lower level */
        case 0b100001: /* instruction abort from same level */
            printf("instruction abort: PC at %#" PRIx64
                   ", address %#" PRIx64 " IFSC %#x %s\n",
                    context->frame->elr, context->far,
                    BITS(context->esr, 5, 0),
                    BIT(ec, 0) ? "" : "user ");

            break;
        case 0b100100: /* data abort from lower level */
        case 0b100101: /* data abort from same level */
            printf("data abort: PC at %#" PRIx64
                   ", address %#" PRIx64 " %s%s\n",
                    context->frame->elr, context->far,
                    BIT(ec, 0) ? "" : "user ",
                    BIT(iss, 6) ? "write" : "read");
    }

    dump_iframe(context->frame);

    // try to dump the user stack
    if (is_user_address(context->frame->usp)) {
        uint8_t buf[256];
        if (copy_from_user_unsafe(buf, (void *)context->frame->usp, sizeof(buf)) == NO_ERROR) {
            printf("bottom of user stack at 0x%lx:\n", (vaddr_t)context->frame->usp);
            hexdump_ex(buf, sizeof(buf), context->frame->usp);
        }
    }
}

void arch_fill_in_exception_context(const arch_exception_context_t *arch_context, mx_exception_report_t *report)
{
    mx_exception_context_t* mx_context = &report->context;

    mx_context->arch_id = ARCH_ID_ARM_64;

    // If there was a fatal page fault, fill in the address that caused the fault.
    if (MX_EXCP_FATAL_PAGE_FAULT == report->header.type) {
        mx_context->arch.u.arm_64.far = arch_context->far;
    } else {
        mx_context->arch.u.arm_64.far = 0;
    }
}
#endif
