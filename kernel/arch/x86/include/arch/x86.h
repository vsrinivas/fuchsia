// Copyright 2016 The Fuchsia Authors
// Copyright (c) 2009 Corey Tabaka
// Copyright (c) 2015 Intel Corporation
// Copyright (c) 2016 Travis Geiselbrecht
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#pragma once

#include <magenta/compiler.h>
#include <sys/types.h>
#include <stdlib.h>
#include <stdbool.h>

#include <arch/x86/general_regs.h>
#include <arch/x86/registers.h>

__BEGIN_CDECLS

#define X86_8BYTE_MASK 0xFFFFFFFF

struct x86_64_iframe {
    uint64_t rdi, rsi, rbp, rbx, rdx, rcx, rax;         // pushed by common handler
    uint64_t r8, r9, r10, r11, r12, r13, r14, r15;      // pushed by common handler
    uint64_t vector;                                    // pushed by stub
    uint64_t err_code;                                  // pushed by interrupt or stub
    uint64_t ip, cs, flags;                             // pushed by interrupt
    uint64_t user_sp, user_ss;                          // pushed by interrupt
};

typedef struct x86_64_iframe x86_iframe_t;

void x86_exception_handler(x86_iframe_t *frame);
enum handler_return platform_irq(x86_iframe_t *frame);

struct arch_exception_context {
    bool is_page_fault;
    x86_iframe_t *frame;
    uint64_t cr2;
};

// Register state layout used by x86_64_context_switch().
struct x86_64_context_switch_frame {
    uint64_t r15, r14, r13, r12;
    uint64_t rbp;
    uint64_t rbx;
    uint64_t rip;
};

struct x86_64_syscall_result {
    // The assembler relies on the fact that the ABI will return this in
    // rax,rdx so we use plain types here to ensure this.
    uint64_t status;
    // Non-zero if thread was signaled.
    uint64_t is_signaled;
};

void x86_64_context_switch(vaddr_t *oldsp, vaddr_t newsp);
void x86_uspace_entry(uintptr_t arg1, uintptr_t arg2, uintptr_t sp,
                      uintptr_t pc, uint64_t rflags) __NO_RETURN;

struct x86_64_syscall_result unknown_syscall(
    uint64_t syscall_num, uint64_t ip);

void x86_syscall(void);

void x86_syscall_process_pending_signals(x86_syscall_general_regs_t *gregs);

/* @brief Register all of the CPUs in the system
 *
 * Must be called only once.
 *
 * @param apic_ids A list of all APIC IDs in the system.  The BP should be in
 *        the list.
 * @param num_cpus The number of entries in the apic_ids list.
 */
void x86_init_smp(uint32_t *apic_ids, uint32_t num_cpus);

/* @brief Bring all of the specified APs up and hand them over to the kernel
 *
 * This function must not be called before x86_init_smp.
 *
 * May be called by any running CPU.  Due to requiring use of the very limited
 * low 1MB of memory, this function is not re-entrant.  Itshould not be executed
 * more than once concurrently.
 *
 * @param apic_ids A list of all APIC IDs to launch.
 * @param count The number of entries in the apic_ids list.
 *
 * @return MX_ERR_INVALID_ARGS if an unknown APIC ID was provided.
 * @return MX_ERR_BAD_STATE if one of the targets is currently online
 * @return MX_ERR_TIMED_OUT if one of the targets failed to launch
 */
status_t x86_bringup_aps(uint32_t *apic_ids, uint32_t count);

#define IO_BITMAP_BITS      65536
#define IO_BITMAP_BYTES     (IO_BITMAP_BITS/8)
#define IO_BITMAP_LONGS     (IO_BITMAP_BITS/sizeof(long))

/*
 * Assignment of Interrupt Stack Table entries
 */
#define NUM_ASSIGNED_IST_ENTRIES 3
#define NMI_IST_INDEX 1
#define MCE_IST_INDEX 2
#define DBF_IST_INDEX 3

/*
 * x86-64 TSS structure
 */
typedef struct {
    uint32_t rsvd0;
    uint64_t rsp0;
    uint64_t rsp1;
    uint64_t rsp2;
    uint32_t rsvd1;
    uint32_t rsvd2;
    uint64_t ist1;
    uint64_t ist2;
    uint64_t ist3;
    uint64_t ist4;
    uint64_t ist5;
    uint64_t ist6;
    uint64_t ist7;
    uint32_t rsvd3;
    uint32_t rsvd4;
    uint16_t rsvd5;
    uint16_t iomap_base;

    uint8_t tss_bitmap[IO_BITMAP_BYTES + 1];
} __PACKED tss_64_t;

typedef tss_64_t tss_t;

static inline void x86_clts(void) {__asm__ __volatile__ ("clts"); }
static inline void x86_hlt(void) {__asm__ __volatile__ ("hlt"); }
static inline void x86_sti(void) {__asm__ __volatile__ ("sti"); }
static inline void x86_cli(void) {__asm__ __volatile__ ("cli"); }
static inline void x86_ltr(uint16_t sel)
{
    __asm__ __volatile__ ("ltr %%ax" :: "a" (sel));
}
static inline void x86_lidt(uintptr_t base)
{
    __asm volatile("lidt (%0)" :: "r"(base) : "memory");
}

static inline uint8_t inp(uint16_t _port)
{
    uint8_t rv;
    __asm__ __volatile__ ("inb %1, %0"
                          : "=a" (rv)
                          : "d" (_port));
    return (rv);
}

static inline uint16_t inpw (uint16_t _port)
{
    uint16_t rv;
    __asm__ __volatile__ ("inw %1, %0"
                          : "=a" (rv)
                          : "d" (_port));
    return (rv);
}

static inline uint32_t inpd(uint16_t _port)
{
    uint32_t rv;
    __asm__ __volatile__ ("inl %1, %0"
                          : "=a" (rv)
                          : "d" (_port));
    return (rv);
}

static inline void outp(uint16_t _port, uint8_t _data)
{
    __asm__ __volatile__ ("outb %1, %0"
                          :
                          : "d" (_port),
                          "a" (_data));
}

static inline void outpw(uint16_t _port, uint16_t _data)
{
    __asm__ __volatile__ ("outw %1, %0"
                          :
                          : "d" (_port),
                          "a" (_data));
}

static inline void outpd(uint16_t _port, uint32_t _data)
{
    __asm__ __volatile__ ("outl %1, %0"
                          :
                          : "d" (_port),
                          "a" (_data));
}

static inline uint64_t rdtsc(void)
{
    uint64_t tsc;

    uint32_t tsc_low;
    uint32_t tsc_hi;

    __asm__ __volatile__("rdtsc" : "=a" (tsc_low), "=d" (tsc_hi));

    tsc = ((uint64_t)tsc_hi << 32) | tsc_low;

     return tsc;
}

static inline void cpuid(uint32_t sel, uint32_t *a, uint32_t *b, uint32_t *c, uint32_t *d)
{
    *a = sel;

    __asm__ __volatile__("cpuid"
                         : "+a" (*a), "=c" (*c), "=d" (*d), "=b"(*b)
                        );
}

/* cpuid wrapper with ecx set to a second argument */
static inline void cpuid_c(uint32_t sel, uint32_t sel_c, uint32_t *a, uint32_t *b, uint32_t *c, uint32_t *d)
{
    *a = sel;
    *c = sel_c;

    __asm__ __volatile__("cpuid"
                         : "+a" (*a), "+c" (*c), "=d" (*d), "=b"(*b)
                        );
}

static inline void set_in_cr0(ulong mask)
{
    ulong temp;

    __asm__ __volatile__ (
        "mov %%cr0, %0  \n\t"
        "or %1, %0      \n\t"
        "mov %0, %%cr0   \n\t"
        : "=r" (temp) : "irg" (mask)
        :);
}

static inline void clear_in_cr0(ulong mask)
{
    ulong temp;

    __asm__ __volatile__ (
        "mov %%cr0, %0  \n\t"
        "and %1, %0     \n\t"
        "mov %0, %%cr0  \n\t"
        : "=r" (temp) : "irg" (~mask)
        :);
}

static inline ulong x86_get_cr2(void)
{
    ulong rv;

    __asm__ __volatile__ (
        "mov %%cr2, %0"
        : "=r" (rv)
    );

    return rv;
}

static inline ulong x86_get_cr3(void)
{
    ulong rv;

    __asm__ __volatile__ (
        "mov %%cr3, %0"
        : "=r" (rv));
    return rv;
}

static inline void x86_set_cr3(ulong in_val)
{
    __asm__ __volatile__ (
        "mov %0,%%cr3 \n\t"
        :
        :"r" (in_val));
}

static inline ulong x86_get_cr0(void)
{
    ulong rv;

    __asm__ __volatile__ (
        "mov %%cr0, %0 \n\t"
        : "=r" (rv));
    return rv;
}

static inline ulong x86_get_cr4(void)
{
    ulong rv;

    __asm__ __volatile__ (
        "mov %%cr4, %0 \n\t"
        : "=r" (rv));
    return rv;
}


static inline void x86_set_cr0(ulong in_val)
{
    __asm__ __volatile__ (
        "mov %0,%%cr0 \n\t"
        :
        :"r" (in_val));
}

static inline void x86_set_cr4(ulong in_val)
{
    __asm__ __volatile__ (
        "mov %0,%%cr4 \n\t"
        :
        :"r" (in_val));
}

#define DEFINE_REGISTER_ACCESSOR(REG)                           \
    static inline void set_##REG(uint16_t value) {              \
        __asm__ volatile("mov %0, %%" #REG : : "r"(value));     \
    }                                                           \
    static inline uint16_t get_##REG(void) {                    \
        uint16_t value;                                         \
        __asm__ volatile("mov %%" #REG ", %0" : "=r"(value));   \
        return value;                                           \
    }

DEFINE_REGISTER_ACCESSOR(ds)
DEFINE_REGISTER_ACCESSOR(es)
DEFINE_REGISTER_ACCESSOR(fs)
DEFINE_REGISTER_ACCESSOR(gs)

#undef DEFINE_REGISTER_ACCESSOR

static inline uint64_t read_msr (uint32_t msr_id)
{
    uint32_t msr_read_val_lo;
    uint32_t msr_read_val_hi;

    __asm__ __volatile__ (
        "rdmsr \n\t"
        : "=a" (msr_read_val_lo), "=d" (msr_read_val_hi)
        : "c" (msr_id));

    return ((uint64_t)msr_read_val_hi << 32) | msr_read_val_lo;
}

status_t read_msr_safe(uint32_t msr_id, uint64_t *val);

static inline void write_msr (uint32_t msr_id, uint64_t msr_write_val)
{
    __asm__ __volatile__ (
        "wrmsr \n\t"
        : : "c" (msr_id), "a" (msr_write_val & 0xffffffff), "d" (msr_write_val >> 32));
}


static inline bool x86_is_paging_enabled(void)
{
    if (x86_get_cr0() & X86_CR0_PG)
        return true;

    return false;
}

static inline bool x86_is_PAE_enabled(void)
{
    if (x86_is_paging_enabled() == false)
        return false;

    if (!(x86_get_cr4() & X86_CR4_PAE))
        return false;

    return true;
}

static inline uint64_t x86_read_gs_offset64(uintptr_t offset)
{
    uint64_t ret;
    __asm__( "movq  %%gs:%1, %0" : "=r" (ret) : "m" (*(uint64_t *)(offset)));
    return ret;
}

static inline void x86_write_gs_offset64(uintptr_t offset, uint64_t val)
{
    __asm__( "movq  %0, %%gs:%1" : : "ir" (val), "m" (*(uint64_t *)(offset)) : "memory");
}

static inline uint32_t x86_read_gs_offset32(uintptr_t offset)
{
    uint32_t ret;
    __asm__( "movl  %%gs:%1, %0" : "=r" (ret) : "m" (*(uint32_t *)(offset)));
    return ret;
}

static inline void x86_write_gs_offset32(uintptr_t offset, uint32_t val)
{
    __asm__( "movl   %0, %%gs:%1" : : "ir" (val), "m" (*(uint32_t *)(offset)) : "memory");
}

typedef uint64_t x86_flags_t;

static inline uint64_t x86_save_flags(void)
{
    uint64_t state;

    __asm__ volatile(
        "pushfq;"
        "popq %0"
        : "=rm" (state)
        :: "memory");

    return state;
}

static inline void x86_restore_flags(uint64_t flags)
{
    __asm__ volatile(
        "pushq %0;"
        "popfq"
        :: "g" (flags)
        : "memory", "cc");
}

static inline void inprep(uint16_t _port, uint8_t *_buffer, uint32_t _reads)
{
    __asm__ __volatile__ ("pushfq \n\t"
                          "cli \n\t"
                          "cld \n\t"
                          "rep insb \n\t"
                          "popfq \n\t"
                          :
                          : "d" (_port),
                          "D" (_buffer),
                          "c" (_reads));
}

static inline void outprep(uint16_t _port, uint8_t *_buffer, uint32_t _writes)
{
    __asm__ __volatile__ ("pushfq \n\t"
                          "cli \n\t"
                          "cld \n\t"
                          "rep outsb \n\t"
                          "popfq \n\t"
                          :
                          : "d" (_port),
                          "S" (_buffer),
                          "c" (_writes));
}

static inline void inpwrep(uint16_t _port, uint16_t *_buffer, uint32_t _reads)
{
    __asm__ __volatile__ ("pushfq \n\t"
                          "cli \n\t"
                          "cld \n\t"
                          "rep insw \n\t"
                          "popfq \n\t"
                          :
                          : "d" (_port),
                          "D" (_buffer),
                          "c" (_reads));
}

static inline void outpwrep(uint16_t _port, uint16_t *_buffer,
                            uint32_t _writes)
{
    __asm__ __volatile__ ("pushfq \n\t"
                          "cli \n\t"
                          "cld \n\t"
                          "rep outsw \n\t"
                          "popfq \n\t"
                          :
                          : "d" (_port),
                          "S" (_buffer),
                          "c" (_writes));
}

static inline void inpdrep(uint16_t _port, uint32_t *_buffer,
                           uint32_t _reads)
{
    __asm__ __volatile__ ("pushfq \n\t"
                          "cli \n\t"
                          "cld \n\t"
                          "rep insl \n\t"
                          "popfq \n\t"
                          :
                          : "d" (_port),
                          "D" (_buffer),
                          "c" (_reads));
}

static inline void outpdrep(uint16_t _port, uint32_t *_buffer,
                            uint32_t _writes)
{
    __asm__ __volatile__ ("pushfq \n\t"
                          "cli \n\t"
                          "cld \n\t"
                          "rep outsl \n\t"
                          "popfq \n\t"
                          :
                          : "d" (_port),
                          "S" (_buffer),
                          "c" (_writes));
}

__END_CDECLS
