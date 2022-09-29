// Copyright 2016 The Fuchsia Authors
// Copyright (c) 2009 Corey Tabaka
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <asm.h>
#include <arch/x86/mp.h>
#include <zircon/errors.h>

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

/* void x86_enable_ints_and_hlt(); */
FUNCTION(x86_enable_ints_and_hlt)
    sti
    hlt  /* HLT must immediately follow STI; it executes in the interrupt shadow */
    RET_AND_SPECULATION_POSTFENCE
END_FUNCTION(x86_enable_ints_and_hlt)


/* zx_status_t read_msr_safe(uint32_t msr_id, uint64_t *val); */
FUNCTION(read_msr_safe)
    // Set up MSR index
    mov %rdi, %rcx

    // Disable interrupts before touching percpu state
    pushfq
    cli

    // Set up the GPF handler, in case the MSR doesn't exist
    leaq .Lgpf_handler(%rip), %rax
    movq %rax, %gs:PERCPU_GPF_RETURN_OFFSET
    rdmsr

    // Cleanup the GPF handler
    movq $0, %gs:PERCPU_GPF_RETURN_OFFSET
    // Restore interrupts if they were on before
    popfq

    // rdmsr returns value via edx:eax
    shl $32, %rdx
    or %rax, %rdx
    mov %rdx, (%rsi)

    mov $ZX_OK, %rax
    RET_AND_SPECULATION_POSTFENCE
.Lgpf_handler:
    // Cleanup GPF handler
    movq $0, %gs:PERCPU_GPF_RETURN_OFFSET
    // Restore interrupts if they were on before
    popfq

    mov $ZX_ERR_NOT_SUPPORTED, %rax
    RET_AND_SPECULATION_POSTFENCE
END_FUNCTION(read_msr_safe)

/* zx_status_t write_msr_safe(uint32_t msr_id, uint64_t val); */
FUNCTION(write_msr_safe)
    // Disable interrupts before touching percpu state
    pushfq
    cli

    // If we take a #GP fault, vector to write_msr_safe_gpf
    leaq .Lwrite_msr_safe_gpf(%rip), %rax
    movq %rax, %gs:PERCPU_GPF_RETURN_OFFSET

    // %rdi holds |msr_id|, %rsi holds |val|
    // we need |msr_id| in %rcx and |val| in %edi:%eax
    movq %rdi, %rcx
    movl %esi, %eax
    movq %rsi, %rdx
    shrq $32, %rdx

    wrmsr
    // We made it here, no #GP. Cleanup the handler and restore interrupt state.
    movq $0, %gs:PERCPU_GPF_RETURN_OFFSET
    popfq
    movq $ZX_OK, %rax
    RET_AND_SPECULATION_POSTFENCE

.Lwrite_msr_safe_gpf:
    movq $0, %gs:PERCPU_GPF_RETURN_OFFSET
    popfq
    mov $ZX_ERR_NOT_SUPPORTED, %rax
    RET_AND_SPECULATION_POSTFENCE
END_FUNCTION(write_msr_safe)

/* void x86_enable_ints_and_mwait(uint32_t hints); */
FUNCTION(x86_enable_ints_and_mwait)
    // Load the mwait hints register; clear the extension register
    movl %edi, %eax
    xor %ecx, %ecx
    sti
    mwait /* MWAIT must immediately follow STI; it executes in the interrupt shadow */
    RET_AND_SPECULATION_POSTFENCE
END_FUNCTION(x86_enable_ints_and_mwait)

/* void x86_monitor(void* addr); */
FUNCTION(x86_monitor)
    // Set the address to monitor
    movq %rdi, %rax
    // Clear the monitor extension and hints registers
    xor %ecx, %ecx
    xor %edx, %edx
    monitor
    RET_AND_SPECULATION_POSTFENCE
END_FUNCTION(x86_monitor)

// void x86_ras_fill(void)
// Fill/overwrite the Return Address Stack with known-good entries, to prevent:
// 1. A RAS underflow forcing a fallback to an Indirect Branch Predictor
//
//    If the RAS is 'empty', the next RET-without-a-call may fall back to the
//    (attackable) BTB/indirect prediction structures on Intel Skylake (&
//    late-stepping Broadwells) CPUs.
//
//    Filling the RAS prevents that attack.
//
// 2. If the RAS is filled with hostile addresses, the next RET-without-a-call
//    will speculatively vector to the hostile code, as in the ret2spec_sa
//    Safeside example.
//
//    Overwriting the RAS prevents those attacks.
FUNCTION(x86_ras_fill)
    pushq %rcx
    // Execute a loop of 32 calls; each call creates a return address stack
    // (RAS/RSB) entry pointing at the predicted return address, |trap|.
    // Any speculative returns will execute code starting at |trap|, which
    // is an infinite loop of PAUSE-LFENCE (a "speculation trap").
    //
    // All known-bad x86 CPUs have a RAS with 32 entries or less. (AMD
    // Mitigation V2-3, Intel Table 6: RSB stuffing).
    movl $16, %ecx

    .align 16
0:  call 1f
.Ltrap1:
    pause
    lfence
    jmp .Ltrap1

    .align 16
1:  call 2f
.Ltrap2:
    pause
    lfence
    jmp .Ltrap2

    .align 16
2:
    subl $1, %ecx
    jnz 0b
    /* Prevent speculation past JNZ, until RAS is overwritten */
    lfence

    addq $256, %rsp
    popq %rcx
    RET_AND_SPECULATION_POSTFENCE
END_FUNCTION(x86_ras_fill)
