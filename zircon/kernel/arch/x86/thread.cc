// Copyright 2016 The Fuchsia Authors
// Copyright (c) 2009 Corey Tabaka
// Copyright (c) 2014 Travis Geiselbrecht
// Copyright (c) 2015 Intel Corporation
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <align.h>
#include <assert.h>
#include <debug.h>
#include <lib/arch/intrin.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>

#include <arch/x86.h>
#include <arch/x86/descriptor.h>
#include <arch/x86/feature.h>
#include <arch/x86/mp.h>
#include <arch/x86/platform_access.h>
#include <arch/x86/registers.h>
#include <kernel/spinlock.h>
#include <kernel/thread.h>

void arch_thread_initialize(Thread* t, vaddr_t entry_point) {
  // create a default stack frame on the stack
  vaddr_t stack_top = t->stack().top();

  // make sure the top of the stack is 16 byte aligned for ABI compliance
  DEBUG_ASSERT(IS_ALIGNED(stack_top, 16));

  // make sure we start the frame 8 byte unaligned (relative to the 16 byte alignment) because
  // of the way the context switch will pop the return address off the stack. After the first
  // context switch, this leaves the stack unaligned relative to how a called function expects it.
  stack_top -= 8;
  struct x86_64_context_switch_frame* frame = (struct x86_64_context_switch_frame*)(stack_top);

  // Record a zero return address so that backtraces will stop here.
  // Otherwise if heap debugging is on, and say there is 99..99 here,
  // then the debugger could try to continue the backtrace from there.
  memset((void*)stack_top, 0, 8);

  // move down a frame size and zero it out
  frame--;
  memset(frame, 0, sizeof(*frame));

  frame->rip = entry_point;

  // initialize the saved extended register state
  vaddr_t buf = ROUNDUP(((vaddr_t)t->arch().extended_register_buffer), 64);
  __UNUSED size_t overhead = buf - (vaddr_t)t->arch().extended_register_buffer;
  DEBUG_ASSERT(sizeof(t->arch().extended_register_buffer) - overhead >=
               x86_extended_register_size());
  t->arch().extended_register_state = (vaddr_t*)buf;
  x86_extended_register_init_state(t->arch().extended_register_state);

  // set the stack pointer
  t->arch().sp = (vaddr_t)frame;
#if __has_feature(safe_stack)
  DEBUG_ASSERT(IS_ALIGNED(t->stack().unsafe_top(), 16));
  t->arch().unsafe_sp = t->stack().unsafe_top();
#endif

  // initialize the fs, gs and kernel bases to 0.
  t->arch().fs_base = 0;
  t->arch().gs_base = 0;

  // Initialize the debug registers to a valid initial state.
  t->arch().track_debug_state = false;
  for (size_t i = 0; i < 4; i++) {
    t->arch().debug_state.dr[i] = 0;
  }
  t->arch().debug_state.dr6 = X86_DR6_MASK;
  t->arch().debug_state.dr7 = X86_DR7_MASK;
}

void arch_thread_construct_first(Thread* t) {}

void arch_dump_thread(Thread* t) {
  if (t->state() != THREAD_RUNNING) {
    dprintf(INFO, "\tarch: ");
    dprintf(INFO, "sp %#" PRIxPTR "\n", t->arch().sp);
  }
}

void* arch_thread_get_blocked_fp(Thread* t) {
  if (!WITH_FRAME_POINTERS)
    return nullptr;

  struct x86_64_context_switch_frame* frame = (struct x86_64_context_switch_frame*)t->arch().sp;

  return (void*)frame->rbp;
}

static void x86_context_switch_spec_mitigations(Thread* oldthread, Thread* newthread) {
  // Spectre V2: Overwrite the Return Address Stack to ensure its not poisoned
  // Only overwrite/fill if the prior thread was a user thread or if we're on CPUs vulnerable to
  // RSB underflow attacks.
  if (x86_cpu_should_ras_fill_on_ctxt_switch() &&
      (oldthread->aspace() || x86_cpu_vulnerable_to_rsb_underflow())) {
    x86_ras_fill();
  }
  auto* const percpu = x86_get_percpu();
  // Flush Indirect Branch Predictor State, if:
  // 1) We are switching from a user address space to another user address space OR
  // 2) We are switching from the kernel address space to a user address space and the
  //    new user address space is not the same as the last user address space that ran
  //    on this core.
  // TODO(fxbug.dev/39621): Handle aspace* reuse.
  if (x86_cpu_should_ibpb_on_ctxt_switch() &&
      (((oldthread->aspace() && newthread->aspace()) &&
        (oldthread->aspace() != newthread->aspace())) ||
       ((!oldthread->aspace() && newthread->aspace()) &&
        (percpu->last_user_aspace != newthread->aspace())))) {
    MsrAccess msr;
    x86_cpu_ibpb(&msr);
  }
  if (oldthread->aspace() && !newthread->aspace()) {
    percpu->last_user_aspace = oldthread->aspace();
  }
}

static void x86_segment_selector_save_state(Thread* thread) {
  // Save the user fs_base and gs_base.  The new rdfsbase instruction is much faster than reading
  // the MSR, so use the former when available.
  if (likely(g_x86_feature_fsgsbase)) {
    thread->arch().fs_base = _readfsbase_u64();
    // Remember, the user and kernel gs_base values have been swapped -- the user value is currently
    // in KERNEL_GS_BASE.
    __asm__ __volatile__("swapgs\n");
    thread->arch().gs_base = _readgsbase_u64();
    __asm__ __volatile__("swapgs\n");
  } else {
    thread->arch().fs_base = read_msr(X86_MSR_IA32_FS_BASE);
    thread->arch().gs_base = read_msr(X86_MSR_IA32_KERNEL_GS_BASE);
  }
}

static void x86_segment_selector_restore_state(Thread* thread) {
  set_ds(0);
  set_es(0);
  set_fs(0);
  if (get_gs() != 0) {
    // Assigning to %gs may clobber gs_base, so we must restore gs_base afterwards.
    uintptr_t gs_base = (uintptr_t)x86_get_percpu();
    set_gs(0);
    write_msr(X86_MSR_IA32_GS_BASE, gs_base);
  }

  // Restore fs_base and save+restore user gs_base.  Note that the user and kernel gs_base values
  // have been swapped -- the user value is currently in KERNEL_GS_BASE.
  if (likely(g_x86_feature_fsgsbase)) {
    _writefsbase_u64(thread->arch().fs_base);
    // There is no variant of the {rd,wr}gsbase instructions for accessing KERNEL_GS_BASE, so we
    // wrap those in two swapgs instructions to get the same effect.  This is a little convoluted,
    // but still faster than using the KERNEL_GS_BASE MSRs.
    __asm__ __volatile__("swapgs\n");
    _writegsbase_u64(thread->arch().gs_base);
    __asm__ __volatile__("swapgs\n");
  } else {
    write_msr(X86_MSR_IA32_FS_BASE, thread->arch().fs_base);
    write_msr(X86_MSR_IA32_KERNEL_GS_BASE, thread->arch().gs_base);
  }
}

static void x86_segment_selector_context_switch(Thread* oldthread, Thread* newthread) {
  // Save the user fs_base register value.  The new rdfsbase instruction is much faster than reading
  // the MSR, so use the former in preference.
  if (likely(g_x86_feature_fsgsbase)) {
    oldthread->arch().fs_base = _readfsbase_u64();
  } else {
    oldthread->arch().fs_base = read_msr(X86_MSR_IA32_FS_BASE);
  }

  // The segment selector registers can't be preserved across context switches in all cases, because
  // some values get clobbered when returning from interrupts.  If an interrupt occurs when a
  // userland process has set %fs = 1 (for example), the IRET instruction used for returning from
  // the interrupt will reset %fs to 0.
  //
  // To prevent the segment selector register values from leaking between processes, we reset these
  // registers across context switches.
  set_ds(0);
  set_es(0);
  set_fs(0);
  if (get_gs() != 0) {
    // Assigning to %gs may clobber gs_base, so we must restore gs_base afterwards.
    DEBUG_ASSERT(arch_ints_disabled());
    uintptr_t gs_base = (uintptr_t)x86_get_percpu();
    set_gs(0);
    write_msr(X86_MSR_IA32_GS_BASE, gs_base);
  }

  // Restore fs_base and save+restore user gs_base.  Note that the user and kernel gs_base values
  // have been swapped -- the user value is currently in KERNEL_GS_BASE.
  if (likely(g_x86_feature_fsgsbase)) {
    // There is no variant of the {rd,wr}gsbase instructions for accessing KERNEL_GS_BASE, so we
    // wrap those in two swapgs instructions to get the same effect.  This is a little convoluted,
    // but still faster than using the KERNEL_GS_BASE MSRs.
    __asm__ __volatile__(
        "swapgs\n"
        "rdgsbase %[old_gsbase]\n"
        "wrgsbase %[new_gsbase]\n"
        "swapgs\n"
        "wrfsbase %[new_fsbase]\n"
        : [old_gsbase] "=&r"(oldthread->arch().gs_base)
        : [new_gsbase] "r"(newthread->arch().gs_base), [new_fsbase] "r"(newthread->arch().fs_base));
  } else {
    oldthread->arch().gs_base = read_msr(X86_MSR_IA32_KERNEL_GS_BASE);
    write_msr(X86_MSR_IA32_FS_BASE, newthread->arch().fs_base);
    write_msr(X86_MSR_IA32_KERNEL_GS_BASE, newthread->arch().gs_base);
  }
}

static void x86_debug_context_switch(Thread* old_thread, Thread* new_thread) {
  // If the new thread has debug state, then install it, replacing the current contents.
  if (unlikely(new_thread->arch().track_debug_state)) {
    // NOTE: There is no enable debug state call, as x86 doesn't have a global enable/disable
    //       switch, but rather enables particular registers through DR7. These registers are
    //       selected by userspace (and filtered by zircon) in the thread_write_state state
    //       syscall.
    //
    //       This means that just writing the thread debug state into the CPU is enough to
    //       activate the debug functionality.
    x86_write_hw_debug_regs(&new_thread->arch().debug_state);
    return;
  }

  // If the old thread had debug state running and the new one doesn't use it, disable the
  // debug capabilities.
  if (unlikely(old_thread->arch().track_debug_state)) {
    x86_disable_debug_state();
  }
}

static void x86_debug_restore_state(Thread* thread) {
  // If |thread| has debug state, restore it, which enables it.
  if (unlikely(thread->arch().track_debug_state)) {
    x86_write_hw_debug_regs(&thread->arch().debug_state);
  } else {
    // We don't know if the currenty CPU has debugging enable or not, but we do know that |thread|
    // shouldn't have it enabled so disable.
    x86_disable_debug_state();
  }
}

// The target fsgsbase attribute allows the compiler to make use of fsgsbase instructions.  While
// this function does not use fsgsbase instructions directly, it calls
// |x86_segment_selector_context_switch|, which does.  By adding the attribute here, we enable the
// compiler to inline |x86_segment_selector_context_switch| into this function.
void arch_context_switch(Thread* oldthread, Thread* newthread) {
  // set the tss SP0 value to point at the top of our stack
  x86_set_tss_sp(newthread->stack().top());

  if (likely(!oldthread->IsUserStateSavedLocked())) {
    x86_extended_register_context_switch(oldthread, newthread);
    x86_debug_context_switch(oldthread, newthread);
    x86_segment_selector_context_switch(oldthread, newthread);
  } else {
    // Nothing left to save for |oldthread|, so just restore |newthread|.  Technically, we could
    // skip restoring here since we know a higher layer will restore before leaving the kernel.  We
    // restore anyway to so we don't leave |oldthread|'s state lingering in the hardware registers.
    x86_extended_register_restore_state(newthread->arch().extended_register_state);
    x86_debug_restore_state(newthread);
    x86_segment_selector_restore_state(newthread);
  }

  x86_context_switch_spec_mitigations(oldthread, newthread);

  x86_64_context_switch(&oldthread->arch().sp, newthread->arch().sp
#if __has_feature(safe_stack)
                        ,
                        &oldthread->arch().unsafe_sp, newthread->arch().unsafe_sp
#endif
  );
}

void arch_save_user_state(Thread* thread) {
  x86_extended_register_save_state(thread->arch().extended_register_state);
  // Not saving debug state because the arch_thread_t's debug state is authoritative.
  x86_segment_selector_save_state(thread);
}

void arch_restore_user_state(Thread* thread) {
  x86_segment_selector_restore_state(thread);
  x86_debug_restore_state(thread);
  x86_extended_register_restore_state(thread->arch().extended_register_state);
}

void arch_set_suspended_general_regs(struct Thread* thread, GeneralRegsSource source, void* gregs) {
  DEBUG_ASSERT(thread->arch().suspended_general_regs.gregs == nullptr);
  DEBUG_ASSERT(gregs != nullptr);
  DEBUG_ASSERT_MSG(source == GeneralRegsSource::Iframe || source == GeneralRegsSource::Syscall,
                   "invalid source %u\n", static_cast<uint32_t>(source));
  thread->arch().general_regs_source = source;
  thread->arch().suspended_general_regs.gregs = gregs;
}

void arch_reset_suspended_general_regs(struct Thread* thread) {
  thread->arch().general_regs_source = GeneralRegsSource::None;
  thread->arch().suspended_general_regs.gregs = nullptr;
}
