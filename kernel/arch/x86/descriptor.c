// Copyright 2016 The Fuchsia Authors
// Copyright (c) 2009 Corey Tabaka
// Copyright (c) 2016 Travis Geiselbrecht
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT


#include <arch/x86.h>
#include <arch/x86/descriptor.h>
#include <arch/x86/idt.h>
#include <arch/x86/interrupts.h>
#include <arch/x86/mp.h>
#include <arch/arch_ops.h>
#include <assert.h>
#include <magenta/compiler.h>
#include <err.h>
#include <string.h>
#include <trace.h>

/* the main global gdt in the system, declared in assembly */
extern uint8_t _gdt[];
static void x86_tss_assign_ists(struct x86_percpu *percpu, tss_t *tss);

void x86_initialize_percpu_tss(void)
{
    struct x86_percpu *percpu = x86_get_percpu();
    uint8_t cpu_num = percpu->cpu_num;
    tss_t *tss = &percpu->default_tss;
    memset(tss, 0, sizeof(*tss));

#if ARCH_X86_32
    tss->esp0 = 0;
    tss->ss0 = DATA_SELECTOR;
    tss->ss1 = 0;
    tss->ss2 = 0;
    tss->eflags = 0x00003002;
    tss->bitmap = offsetof(tss_32_t, tss_bitmap);
    tss->trace = 1; // trap on hardware task switch

    set_global_desc_32(TSS_SELECTOR(cpu_num), (uintptr_t)tss, sizeof(*tss) - 1, 1, 0, 0, SEG_TYPE_TSS, 0, 0);
#elif ARCH_X86_64
    /* zeroed out TSS is okay for now */
    set_global_desc_64(TSS_SELECTOR(cpu_num), (uintptr_t)tss, sizeof(*tss) - 1, 1, 0, 0, SEG_TYPE_TSS, 0, 0);

    x86_tss_assign_ists(percpu, tss);

    tss->iomap_base = offsetof(tss_64_t, tss_bitmap);

    // Setup alternate stacks to gurantee stack sanity when handling these
    // interrupts
    idt_set_ist_index(&percpu->idt, X86_INT_NMI, NMI_IST_INDEX);
    idt_set_ist_index(&percpu->idt, X86_INT_MACHINE_CHECK, MCE_IST_INDEX);
    idt_set_ist_index(&percpu->idt, X86_INT_DOUBLE_FAULT, DBF_IST_INDEX);
#endif
    // Need to have an extra byte at the end of the bitmap because it will always potentially read two bytes
    tss->tss_bitmap[IO_BITMAP_BYTES] = 0xff;

    x86_ltr(TSS_SELECTOR(cpu_num));
}

#ifdef ARCH_X86_64
static void x86_tss_assign_ists(struct x86_percpu *percpu, tss_t *tss)
{
    tss->ist1 = (uintptr_t)&percpu->interrupt_stacks[0] + PAGE_SIZE;
    tss->ist2 = (uintptr_t)&percpu->interrupt_stacks[1] + PAGE_SIZE;
    tss->ist3 = (uintptr_t)&percpu->interrupt_stacks[2] + PAGE_SIZE;
}
#endif

void x86_set_tss_sp(vaddr_t sp)
{
    tss_t *tss = &x86_get_percpu()->default_tss;
#if ARCH_X86_32
    tss->esp0 = sp;
#elif ARCH_X86_64
    tss->rsp0 = sp;
#endif
}

void x86_set_tss_io_bitmap(uint8_t *bitmap)
{
    DEBUG_ASSERT(arch_ints_disabled());
    tss_t *tss = &x86_get_percpu()->default_tss;
    memcpy(tss->tss_bitmap, bitmap, IO_BITMAP_BYTES);
}

void x86_clear_tss_io_bitmap(void)
{
    DEBUG_ASSERT(arch_ints_disabled());
    tss_t *tss = &x86_get_percpu()->default_tss;
    memset(tss->tss_bitmap, 0xff, IO_BITMAP_BYTES);
}

void set_global_desc_32(seg_sel_t sel, uint32_t base, uint32_t limit,
                                      uint8_t present, uint8_t ring, uint8_t sys,
                                      uint8_t type, uint8_t gran, uint8_t bits)
{
    // 16/32 bit descriptor structure
    struct seg_desc_32 {
        uint16_t limit_15_0;
        uint16_t base_15_0;
        uint8_t base_23_16;

        uint8_t type : 4;
        uint8_t s : 1;
        uint8_t dpl : 2;
        uint8_t p : 1;

        uint8_t limit_19_16 : 4;
        uint8_t avl : 1;
        uint8_t reserved_0 : 1;
        uint8_t d_b : 1;
        uint8_t g : 1;

        uint8_t base_31_24;
    } __PACKED;

    struct seg_desc_32 entry = { 0 };

    entry.limit_15_0 = limit & 0x0000ffff;
    entry.limit_19_16 = (limit & 0x000f0000) >> 16;

    entry.base_15_0 = base & 0x0000ffff;
    entry.base_23_16 = (base & 0x00ff0000) >> 16;
    entry.base_31_24 = base >> 24;

    entry.type = type & 0x0f; // segment type
    entry.p = present != 0;   // present
    entry.dpl = ring & 0x03;  // descriptor privilege level
    entry.g = gran != 0;      // granularity
    entry.s = sys != 0;       // system / non-system
    entry.d_b = bits != 0;    // 16 / 32 bit

    // copy it into the appropriate entry
    uint16_t index = sel >> 3;

    // index is in units of 8 bytes into the gdt table
    struct seg_desc_32 *g = (struct seg_desc_32 *)(_gdt + index * 8);
    *g = entry;
}

void set_global_desc_64(seg_sel_t sel, uint64_t base, uint32_t limit,
                                      uint8_t present, uint8_t ring, uint8_t sys,
                                      uint8_t type, uint8_t gran, uint8_t bits)
{
    // 64 bit descriptor structure
    struct seg_desc_64 {
        uint16_t limit_15_0;
        uint16_t base_15_0;
        uint8_t base_23_16;

        uint8_t type : 4;
        uint8_t s : 1;
        uint8_t dpl : 2;
        uint8_t p : 1;

        uint8_t limit_19_16 : 4;
        uint8_t avl : 1;
        uint8_t reserved_0 : 1;
        uint8_t d_b : 1;
        uint8_t g : 1;

        uint8_t base_31_24;

        uint32_t base_63_32;
        uint32_t reserved_sbz;
    } __PACKED;

    struct seg_desc_64 entry = { 0 };

    entry.limit_15_0 = limit & 0x0000ffff;
    entry.limit_19_16 = (limit & 0x000f0000) >> 16;

    entry.base_15_0 = base & 0x0000ffff;
    entry.base_23_16 = (base & 0x00ff0000) >> 16;
    entry.base_31_24 = (base & 0xff000000) >> 24;
    entry.base_63_32 = base >> 32;

    entry.type = type & 0x0f; // segment type
    entry.p = present != 0;   // present
    entry.dpl = ring & 0x03;  // descriptor privilege level
    entry.g = gran != 0;      // granularity
    entry.s = sys != 0;       // system / non-system
    entry.d_b = bits != 0;    // 16 / 32 bit

    // copy it into the appropriate entry
    uint16_t index = sel >> 3;

    // index is in units of 8 bytes into the gdt table
    struct seg_desc_64 *g = (struct seg_desc_64 *)(_gdt + index * 8);
    *g = entry;
}

