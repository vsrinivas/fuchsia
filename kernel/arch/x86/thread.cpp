// Copyright 2016 The Fuchsia Authors
// Copyright (c) 2009 Corey Tabaka
// Copyright (c) 2014 Travis Geiselbrecht
// Copyright (c) 2015 Intel Corporation
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <assert.h>
#include <sys/types.h>
#include <string.h>
#include <stdlib.h>
#include <debug.h>
#include <kernel/thread.h>
#include <kernel/spinlock.h>
#include <arch/x86.h>
#include <arch/x86/descriptor.h>
#include <arch/x86/mp.h>
#include <arch/x86/registers.h>

void arch_thread_initialize(thread_t *t, vaddr_t entry_point)
{
    // create a default stack frame on the stack
    vaddr_t stack_top = (vaddr_t)t->stack + t->stack_size;

    // make sure the top of the stack is 16 byte aligned for ABI compliance
    stack_top = ROUNDDOWN(stack_top, 16);
    t->stack_top = stack_top;

    // make sure we start the frame 8 byte unaligned (relative to the 16 byte alignment) because
    // of the way the context switch will pop the return address off the stack. After the first
    // context switch, this leaves the stack in unaligned relative to how a called function expects it.
    stack_top -= 8;
    struct x86_64_context_switch_frame *frame = (struct x86_64_context_switch_frame *)(stack_top);

    // Record a zero return address so that backtraces will stop here.
    // Otherwise if heap debugging is on, and say there is 99..99 here,
    // then the debugger could try to continue the backtrace from there.
    memset((void*) stack_top, 0, 8);

    // move down a frame size and zero it out
    frame--;
    memset(frame, 0, sizeof(*frame));

    frame->rip = entry_point;

    // initialize the saved extended register state
    vaddr_t buf = ROUNDUP(((vaddr_t)t->arch.extended_register_buffer), 64);
    __UNUSED size_t overhead = buf - (vaddr_t)t->arch.extended_register_buffer;
    DEBUG_ASSERT(sizeof(t->arch.extended_register_buffer) - overhead >=
            x86_extended_register_size());
    t->arch.extended_register_state = (vaddr_t *)buf;
    x86_extended_register_init_state(t->arch.extended_register_state);

    // set the stack pointer
    t->arch.sp = (vaddr_t)frame;
#if __has_feature(safe_stack)
    t->arch.unsafe_sp =
        ROUNDDOWN((vaddr_t)t->unsafe_stack + t->stack_size, 16);
#endif

    // initialize the fs, gs and kernel bases to 0.
    t->arch.fs_base = 0;
    t->arch.gs_base = 0;
}

void arch_thread_construct_first(thread_t *t)
{
}

void arch_dump_thread(thread_t *t)
{
    if (t->state != THREAD_RUNNING) {
        dprintf(INFO, "\tarch: ");
        dprintf(INFO, "sp %#" PRIxPTR "\n", t->arch.sp);
    }
}

__NO_SAFESTACK
void arch_context_switch(thread_t *oldthread, thread_t *newthread)
{
    x86_extended_register_context_switch(oldthread, newthread);

    //printf("cs 0x%llx\n", kstack_top);

    /* set the tss SP0 value to point at the top of our stack */
    x86_set_tss_sp(newthread->stack_top);

    /* user and kernel gs have been swapped, so unswap them when loading
     * from the msrs
     */
    oldthread->arch.fs_base = read_msr(X86_MSR_IA32_FS_BASE);
    oldthread->arch.gs_base = read_msr(X86_MSR_IA32_KERNEL_GS_BASE);

    /* The segment selector registers can't be preserved across context
     * switches in all cases, because some values get clobbered when
     * returning from interrupts.  If an interrupt occurs when a userland
     * process has set %fs = 1 (for example), the IRET instruction used for
     * returning from the interrupt will reset %fs to 0.
     *
     * To prevent the segment selector register values from leaking between
     * processes, we reset these registers across context switches. */
    set_ds(0);
    set_es(0);
    set_fs(0);
    if (get_gs() != 0) {
        /* Assigning to %gs clobbers gs_base, so we must restore gs_base
         * afterwards. */
        DEBUG_ASSERT(arch_ints_disabled());
        uintptr_t gs_base = (uintptr_t)x86_get_percpu();
        set_gs(0);
        write_msr(X86_MSR_IA32_GS_BASE, gs_base);
    }

    write_msr(X86_MSR_IA32_FS_BASE, newthread->arch.fs_base);
    write_msr(X86_MSR_IA32_KERNEL_GS_BASE, newthread->arch.gs_base);

#if __has_feature(safe_stack)
    oldthread->arch.unsafe_sp = x86_read_gs_offset64(MX_TLS_UNSAFE_SP_OFFSET);
    x86_write_gs_offset64(MX_TLS_UNSAFE_SP_OFFSET, newthread->arch.unsafe_sp);
#endif

    x86_64_context_switch(&oldthread->arch.sp, newthread->arch.sp);
}
