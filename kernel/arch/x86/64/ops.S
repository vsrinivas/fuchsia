// Copyright 2016 The Fuchsia Authors
// Copyright (c) 2009 Corey Tabaka
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <asm.h>
#include <arch/x86/mp.h>
#include <err.h>

.text

/* This follows the x86-64 ABI, the parameters are stored in registers in the following order*/
/*
%rdi used to pass 1st argument
%rsi used to pass 2nd argument
%rdx used to pass 3rd argument and 2nd return register
%rcx used to pass 4th argument
%r8 used to pass 5th argument
%r9 used to pass 6th argument
%rax 1st return register
*/

/* void arch_idle(); */
FUNCTION(arch_idle)
    pushf
    popq %rax
    andq $0x200, %rax
    test %rax, %rax
    je 1f                   /* don't halt if local interrupts are disabled */
    hlt
1:
    ret
END_FUNCTION(arch_idle)


/* status_t read_msr_safe(uint32_t msr_id, uint64_t *val); */
FUNCTION(read_msr_safe)
    # Set up MSR index
    mov %rdi, %rcx

    # Disable interrupts before touching percpu state
    pushfq
    cli

    # Set up the GPF handler, in case the MSR doesn't exist
    movq $.Lgpf_handler, %gs:PERCPU_GPF_RETURN_OFFSET
    rdmsr

    # Cleanup the GPF handler
    movq $0, %gs:PERCPU_GPF_RETURN_OFFSET
    # Restore interrupts if they were on before
    popfq

    # rdmsr returns value via edx:eax
    shl $32, %rdx
    or %rax, %rdx
    mov %rdx, (%rsi)

    mov $MX_OK, %rax
    ret
.Lgpf_handler:
    # Cleanup GPF handler
    movq $0, %gs:PERCPU_GPF_RETURN_OFFSET
    # Restore interrupts if they were on before
    popfq

    mov $MX_ERR_NOT_SUPPORTED, %rax
    ret
END_FUNCTION(read_msr_safe)
