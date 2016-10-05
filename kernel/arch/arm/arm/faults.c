// Copyright 2016 The Fuchsia Authors
// Copyright (c) 2008-2014 Travis Geiselbrecht
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <debug.h>
#include <bits.h>
#include <err.h>
#include <inttypes.h>
#include <arch/arm.h>
#include <kernel/thread.h>
#include <platform.h>

#if WITH_LIB_MAGENTA
#include <lib/user_copy.h>
#include <magenta/exception.h>
#endif

struct fault_handler_table_entry {
    uint32_t pc;
    uint32_t fault_handler;
};

extern struct fault_handler_table_entry __fault_handler_table_start[];
extern struct fault_handler_table_entry __fault_handler_table_end[];

static void dump_mode_regs(uint32_t spsr, uint32_t svc_r13, uint32_t svc_r14)
{
    struct arm_mode_regs regs;
    arm_save_mode_regs(&regs);

    dprintf(CRITICAL, "%c%s r13 0x%08x r14 0x%08x\n", ((spsr & CPSR_MODE_MASK) == CPSR_MODE_USR) ? '*' : ' ', "usr", regs.usr_r13, regs.usr_r14);
    dprintf(CRITICAL, "%c%s r13 0x%08x r14 0x%08x\n", ((spsr & CPSR_MODE_MASK) == CPSR_MODE_FIQ) ? '*' : ' ', "fiq", regs.fiq_r13, regs.fiq_r14);
    dprintf(CRITICAL, "%c%s r13 0x%08x r14 0x%08x\n", ((spsr & CPSR_MODE_MASK) == CPSR_MODE_IRQ) ? '*' : ' ', "irq", regs.irq_r13, regs.irq_r14);
    dprintf(CRITICAL, "%c%s r13 0x%08x r14 0x%08x\n", 'a', "svc", regs.svc_r13, regs.svc_r14);
    dprintf(CRITICAL, "%c%s r13 0x%08x r14 0x%08x\n", ((spsr & CPSR_MODE_MASK) == CPSR_MODE_SVC) ? '*' : ' ', "svc", svc_r13, svc_r14);
    dprintf(CRITICAL, "%c%s r13 0x%08x r14 0x%08x\n", ((spsr & CPSR_MODE_MASK) == CPSR_MODE_UND) ? '*' : ' ', "und", regs.und_r13, regs.und_r14);
    dprintf(CRITICAL, "%c%s r13 0x%08x r14 0x%08x\n", ((spsr & CPSR_MODE_MASK) == CPSR_MODE_SYS) ? '*' : ' ', "sys", regs.sys_r13, regs.sys_r14);

    // dump the bottom of the current stack
    addr_t stack;
    switch (spsr & CPSR_MODE_MASK) {
        case CPSR_MODE_FIQ:
            stack = regs.fiq_r13;
            break;
        case CPSR_MODE_IRQ:
            stack = regs.irq_r13;
            break;
        case CPSR_MODE_SVC:
            stack = svc_r13;
            break;
        case CPSR_MODE_UND:
            stack = regs.und_r13;
            break;
        case CPSR_MODE_SYS:
            stack = regs.sys_r13;
            break;
        default:
            stack = 0;
    }

    if (stack != 0) {
        dprintf(CRITICAL, "bottom of stack at 0x%08x:\n", (unsigned int)stack);
        hexdump((void *)stack, 128);
    }
}

static void dump_fault_frame(struct arm_fault_frame *frame)
{
    struct thread *current_thread = get_current_thread();

    dprintf(CRITICAL, "current_thread %p, name %s\n",
            current_thread, current_thread ? current_thread->name : "");

    dprintf(CRITICAL, "r0  0x%08x r1  0x%08x r2  0x%08x r3  0x%08x\n", frame->r[0], frame->r[1], frame->r[2], frame->r[3]);
    dprintf(CRITICAL, "r4  0x%08x r5  0x%08x r6  0x%08x r7  0x%08x\n", frame->r[4], frame->r[5], frame->r[6], frame->r[7]);
    dprintf(CRITICAL, "r8  0x%08x r9  0x%08x r10 0x%08x r11 0x%08x\n", frame->r[8], frame->r[9], frame->r[10], frame->r[11]);
    dprintf(CRITICAL, "r12 0x%08x usp 0x%08x ulr 0x%08x pc  0x%08x\n", frame->r[12], frame->usp, frame->ulr, frame->pc);
    dprintf(CRITICAL, "spsr 0x%08x\n", frame->spsr);

    dump_mode_regs(frame->spsr, (uintptr_t)(frame + 1), frame->lr);
}

static void dump_iframe(struct arm_iframe *frame)
{
    dprintf(CRITICAL, "r0  0x%08x r1  0x%08x r2  0x%08x r3  0x%08x\n", frame->r0, frame->r1, frame->r2, frame->r3);
    dprintf(CRITICAL, "r12 0x%08x usp 0x%08x ulr 0x%08x pc  0x%08x\n", frame->r12, frame->usp, frame->ulr, frame->pc);
    dprintf(CRITICAL, "spsr 0x%08x\n", frame->spsr);

    dump_mode_regs(frame->spsr, (uintptr_t)(frame + 1), frame->lr);
}

static void exception_die(struct arm_fault_frame *frame, const char *msg)
{
    dprintf(CRITICAL, "%s", msg);
    dump_fault_frame(frame);

    platform_halt(HALT_ACTION_HALT, HALT_REASON_SW_PANIC);
}

static void exception_die_iframe(struct arm_iframe *frame, const char *msg)
{
    dprintf(CRITICAL, "%s", msg);
    dump_iframe(frame);

    platform_halt(HALT_ACTION_HALT, HALT_REASON_SW_PANIC);
}

void arm_syscall_handler(struct arm_fault_frame *frame);
__WEAK void arm_syscall_handler(struct arm_fault_frame *frame)
{
    exception_die(frame, "unhandled syscall, halting\n");
}

void arm_undefined_handler(struct arm_iframe *frame);
void arm_undefined_handler(struct arm_iframe *frame)
{
    /* look at the undefined instruction, figure out if it's something we can handle */
    bool in_thumb = frame->spsr & (1<<5);
    if (in_thumb) {
        frame->pc -= 2;
    } else {
        frame->pc -= 4;
    }

    __UNUSED uint32_t opcode = *(uint32_t *)frame->pc;
    //dprintf(CRITICAL, "undefined opcode 0x%x\n", opcode);

#if ARM_WITH_VFP
    if (in_thumb) {
        /* look for a 32bit thumb instruction */
        if (opcode & 0x0000e800) {
            /* swap the 16bit words */
            opcode = (opcode >> 16) | (opcode << 16);
        }

        if (((opcode & 0xec000e00) == 0xec000a00) || // vfp
                ((opcode & 0xef000000) == 0xef000000) || // advanced simd data processing
                ((opcode & 0xff100000) == 0xf9000000)) { // VLD

            //dprintf(CRITICAL, "vfp/neon thumb instruction 0x%08x at 0x%x\n", opcode, frame->pc);
            goto fpu;
        }
    } else {
        /* look for arm vfp/neon coprocessor instructions */
        if (((opcode & 0x0c000e00) == 0x0c000a00) || // vfp
                ((opcode & 0xfe000000) == 0xf2000000) || // advanced simd data processing
                ((opcode & 0xff100000) == 0xf4000000)) { // VLD
            //dprintf(CRITICAL, "vfp/neon arm instruction 0x%08x at 0x%x\n", opcode, frame->pc);
            goto fpu;
        }
    }
#endif

#if WITH_LIB_MAGENTA
    bool user = (frame->spsr & CPSR_MODE_MASK) == CPSR_MODE_USR;
    if (user) {
        // let magenta get a shot at it
        struct arch_exception_context context = { .iframe = true, .frame = frame };
        arch_enable_ints();
        status_t erc = magenta_exception_handler(MX_EXCP_UNDEFINED_INSTRUCTION, &context, frame->pc);
        arch_disable_ints();
        if (erc == NO_ERROR)
            return;
    }
#endif

    exception_die_iframe(frame, "undefined abort, halting\n");
    return;

#if ARM_WITH_VFP
fpu:
    arm_fpu_undefined_instruction(frame);
#endif
}

static status_t arm_shared_page_fault_handler(struct arm_fault_frame *frame, uint32_t fsr, uint32_t far,
        bool instruction_fault)
{
    // decode the fault status (from table B3-23) and see if we need to call into the VMM for a page fault
    uint32_t fault_status = (BIT(fsr, 10) ? (1<<4) : 0) |  BITS(fsr, 3, 0);
    switch (fault_status) {
        case 0b01101:
        case 0b01111: // permission fault
            // XXX add flag for permission
            PANIC_UNIMPLEMENTED;
        case 0b00101:
        case 0b00111: { // translation fault
            bool write = !!BIT(fsr, 11);
            bool user = (frame->spsr & CPSR_MODE_MASK) == CPSR_MODE_USR;

            uint pf_flags = 0;
            pf_flags |= write ? VMM_PF_FLAG_WRITE : 0;
            pf_flags |= user ? VMM_PF_FLAG_USER : 0;
            pf_flags |= instruction_fault ? VMM_PF_FLAG_INSTRUCTION : 0;
            pf_flags |= VMM_PF_FLAG_NOT_PRESENT;

            arch_enable_ints();
            status_t err = vmm_page_fault_handler(far, pf_flags);
            arch_disable_ints();
            return err;
        }
        case 0b00011:
        case 0b00110: // access flag fault
            // XXX not doing access flag yet
            break;
        case 0b01001:
        case 0b01011: // domain fault
            // XXX can we get these?
            break;
    }

    return ERR_INTERNAL;
}

void arm_data_abort_handler(struct arm_fault_frame *frame);
void arm_data_abort_handler(struct arm_fault_frame *frame)
{
    uint32_t fsr = arm_read_dfsr();
    uint32_t far = arm_read_dfar();

    // see if the page fault handler can deal with it
    if (likely(arm_shared_page_fault_handler(frame, fsr, far, false)) == NO_ERROR)
        return;

    // Check if the current thread was expecting a data fault and
    // we should return to its handler.
    thread_t *thr = get_current_thread();
    if (thr->arch.data_fault_resume != NULL) {
        frame->pc = (uintptr_t)thr->arch.data_fault_resume;
        return;
    }

    struct fault_handler_table_entry *fault_handler;
    for (fault_handler = __fault_handler_table_start; fault_handler < __fault_handler_table_end; fault_handler++) {
        if (fault_handler->pc == frame->pc) {
            frame->pc = fault_handler->fault_handler;
            return;
        }
    }

#if WITH_LIB_MAGENTA
    bool user = (frame->spsr & CPSR_MODE_MASK) == CPSR_MODE_USR;
    if (user) {
        // let magenta get a shot at it
        struct arch_exception_context context = { .iframe = false, .frame = frame };
        arch_enable_ints();
        status_t erc = magenta_exception_handler(MX_EXCP_FATAL_PAGE_FAULT, &context, frame->pc);
        arch_disable_ints();
        if (erc == NO_ERROR)
            return;
    }
#endif

    // at this point we're dumping state and panicing
    dprintf(CRITICAL, "\n\ncpu %u data abort, ", arch_curr_cpu_num());
    bool write = !!BIT(fsr, 11);

    /* decode the fault status (from table B3-23) */
    uint32_t fault_status = (BIT(fsr, 10) ? (1<<4) : 0) |  BITS(fsr, 3, 0);
    switch (fault_status) {
        case 0b00001: // alignment fault
            dprintf(CRITICAL, "alignment fault on %s\n", write ? "write" : "read");
            break;
        case 0b00101:
        case 0b00111: // translation fault
            dprintf(CRITICAL, "translation fault on %s\n", write ? "write" : "read");
            break;
        case 0b00011:
        case 0b00110: // access flag fault
            dprintf(CRITICAL, "access flag fault on %s\n", write ? "write" : "read");
            break;
        case 0b01001:
        case 0b01011: // domain fault
            dprintf(CRITICAL, "domain fault, domain %u\n", BITS_SHIFT(fsr, 7, 4));
            break;
        case 0b01101:
        case 0b01111: // permission fault
            dprintf(CRITICAL, "permission fault on %s\n", write ? "write" : "read");
            break;
        case 0b00010: // debug event
            dprintf(CRITICAL, "debug event\n");
            break;
        case 0b01000: // synchronous external abort
            dprintf(CRITICAL, "synchronous external abort on %s\n", write ? "write" : "read");
            break;
        case 0b10110: // asynchronous external abort
            dprintf(CRITICAL, "asynchronous external abort on %s\n", write ? "write" : "read");
            break;
        case 0b10000: // TLB conflict event
        case 0b11001: // synchronous parity error on memory access
        case 0b00100: // fault on instruction cache maintenance
        case 0b01100: // synchronous external abort on translation table walk
        case 0b01110: //    "
        case 0b11100: // synchronous parity error on translation table walk
        case 0b11110: //    "
        case 0b11000: // asynchronous parity error on memory access
        default:
            dprintf(CRITICAL, "unhandled fault\n");
            ;
    }

    dprintf(CRITICAL, "DFAR 0x%x (fault address)\n", far);
    dprintf(CRITICAL, "DFSR 0x%x (fault status register)\n", fsr);

    exception_die(frame, "halting\n");
}

void arm_prefetch_abort_handler(struct arm_fault_frame *frame);
void arm_prefetch_abort_handler(struct arm_fault_frame *frame)
{
    uint32_t fsr = arm_read_ifsr();
    uint32_t far = arm_read_ifar();

    // see if the page fault handler can deal with it
    if (likely(arm_shared_page_fault_handler(frame, fsr, far, true)) == NO_ERROR)
        return;

#if WITH_LIB_MAGENTA
    bool user = (frame->spsr & CPSR_MODE_MASK) == CPSR_MODE_USR;
    if (user) {
        // let magenta get a shot at it
        struct arch_exception_context context = { .iframe = false, .frame = frame };
        arch_enable_ints();
        status_t erc = magenta_exception_handler(MX_EXCP_FATAL_PAGE_FAULT, &context, frame->pc);
        arch_disable_ints();
        if (erc == NO_ERROR)
            return;
    }
#endif

    uint32_t fault_status = (BIT(fsr, 10) ? (1<<4) : 0) |  BITS(fsr, 3, 0);

    dprintf(CRITICAL, "\n\ncpu %u prefetch abort, ", arch_curr_cpu_num());

    /* decode the fault status (from table B3-23) */
    switch (fault_status) {
        case 0b00001: // alignment fault
            dprintf(CRITICAL, "alignment fault\n");
            break;
        case 0b00101:
        case 0b00111: // translation fault
            dprintf(CRITICAL, "translation fault\n");
            break;
        case 0b00011:
        case 0b00110: // access flag fault
            dprintf(CRITICAL, "access flag fault\n");
            break;
        case 0b01001:
        case 0b01011: // domain fault
            dprintf(CRITICAL, "domain fault, domain %u\n", BITS_SHIFT(fsr, 7, 4));
            break;
        case 0b01101:
        case 0b01111: // permission fault
            dprintf(CRITICAL, "permission fault\n");
            break;
        case 0b00010: // debug event
            dprintf(CRITICAL, "debug event\n");
            break;
        case 0b01000: // synchronous external abort
            dprintf(CRITICAL, "synchronous external abort\n");
            break;
        case 0b10110: // asynchronous external abort
            dprintf(CRITICAL, "asynchronous external abort\n");
            break;
        case 0b10000: // TLB conflict event
        case 0b11001: // synchronous parity error on memory access
        case 0b00100: // fault on instruction cache maintenance
        case 0b01100: // synchronous external abort on translation table walk
        case 0b01110: //    "
        case 0b11100: // synchronous parity error on translation table walk
        case 0b11110: //    "
        case 0b11000: // asynchronous parity error on memory access
        default:
            dprintf(CRITICAL, "unhandled fault\n");
            ;
    }

    dprintf(CRITICAL, "IFAR 0x%x (fault address)\n", far);
    dprintf(CRITICAL, "IFSR 0x%x (fault status register)\n", fsr);

    exception_die(frame, "halting\n");
}

#if WITH_LIB_MAGENTA
void arch_dump_exception_context(const arch_exception_context_t *context)
{
    // based on context, this could have been a iframe or a full fault frame
    uint32_t usp = 0;
    if (context->iframe) {
        struct arm_iframe *iframe = context->frame;
        dump_iframe(iframe);
        usp = iframe->usp;
    } else {
        struct arm_fault_frame *frame = context->frame;
        dump_fault_frame(frame);
        usp = frame->usp;
    }

    // try to dump the user stack
    if (is_user_address(usp)) {
        uint8_t buf[256];
        if (copy_from_user_unsafe(buf, (void *)usp, sizeof(buf)) == NO_ERROR) {
            printf("bottom of user stack at %#" PRIxPTR ":\n", (vaddr_t)usp);
            hexdump_ex(buf, sizeof(buf), usp);
        }
    }
}

void arch_fill_in_exception_context(const arch_exception_context_t *arch_context, mx_exception_report_t *report)
{
    report->context.arch_id = ARCH_ID_UNKNOWN;

    // TODO: implement for arm32
}
#endif
