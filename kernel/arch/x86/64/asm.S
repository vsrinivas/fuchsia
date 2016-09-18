// Copyright 2016 The Fuchsia Authors
// Copyright (c) 2015 Travis Geiselbrecht
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <asm.h>
#include <arch/defines.h>

/* void x86_64_context_switch(uint64_t *oldsp, uint64_t newsp) */
FUNCTION(x86_64_context_switch)
    /* save the old context and restore the new */
    pushf
    pushq %rbx
    pushq %rbp
    pushq %r12
    pushq %r13
    pushq %r14
    pushq %r15

    movq %rsp,(%rdi)
    movq %rsi,%rsp

    popq %r15
    popq %r14
    popq %r13
    popq %r12
    popq %rbp
    popq %rbx
    popf

    retq

#if WITH_SMP
/* void arch_spin_lock(unsigned long *lock) */
FUNCTION(arch_spin_lock)
.Lspin:
    cmpq $0, (%rdi)
    je .Ltake_lock
    pause
    jmp .Lspin
.Ltake_lock:
    mov $1, %rax
    xchg %rax, (%rdi)
    cmp $0, %rax // make sure we actually got the lock
    jne .Lspin // if we lost the race, resume waiting
    ret

/* int arch_spin_trylock(unsigned long *lock) */
FUNCTION(arch_spin_trylock)
    mov $1, %rax
    xchg %rax, (%rdi)
    ret // return 0 if we got the lock

/* void arch_spin_unlock(spin_lock_t *lock) */
FUNCTION(arch_spin_unlock)
    mov $0, %rax
    xchg %rax, (%rdi)
    ret
#endif // WITH_SMP

/* rep stos version of page zero */
FUNCTION(arch_zero_page)
    xor     %rax, %rax
    mov     $PAGE_SIZE >> 3, %rcx
    cld

    rep     stosq

    ret
