// Copyright 2016 The Fuchsia Authors
// Copyright (c) 2009 Corey Tabaka
// Copyright (c) 2016 Travis Geiselbrecht
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <arch/arch_ops.h>
#include <arch/x86.h>
#include <arch/x86/descriptor.h>
#include <arch/x86/idt.h>
#include <arch/x86/interrupts.h>
#include <arch/x86/mp.h>
#include <assert.h>
#include <bits.h>
#include <err.h>
#include <string.h>
#include <trace.h>
#include <zircon/compiler.h>

#define TSS_DESC_BUSY_BIT (1ull << 41)

/* the main global gdt in the system, declared in assembly */
extern uint8_t _gdt[];
static void x86_tss_assign_ists(struct x86_percpu* percpu, tss_t* tss);

struct task_desc {
    uint64_t low;
    uint64_t high;
} __PACKED;

void x86_initialize_percpu_tss(void) {
    struct x86_percpu* percpu = x86_get_percpu();
    uint cpu_num = percpu->cpu_num;
    tss_t* tss = &percpu->default_tss;
    memset(tss, 0, sizeof(*tss));

    /* zeroed out TSS is okay for now */
    set_global_desc_64(TSS_SELECTOR(cpu_num), (uintptr_t)tss, sizeof(*tss) - 1, 1, 0, 0, SEG_TYPE_TSS, 0, 0);

    x86_tss_assign_ists(percpu, tss);

    tss->iomap_base = offsetof(tss_64_t, tss_bitmap);
    // Need to have an extra byte at the end of the bitmap because it will always potentially read two bytes
    tss->tss_bitmap[IO_BITMAP_BYTES] = 0xff;

    x86_ltr(TSS_SELECTOR(cpu_num));
}

static void x86_tss_assign_ists(struct x86_percpu* percpu, tss_t* tss) {
    tss->ist1 = (uintptr_t)&percpu->interrupt_stacks[0] + PAGE_SIZE;
    tss->ist2 = (uintptr_t)&percpu->interrupt_stacks[1] + PAGE_SIZE;
    tss->ist3 = (uintptr_t)&percpu->interrupt_stacks[2] + PAGE_SIZE;
}

void x86_set_tss_sp(vaddr_t sp) {
    tss_t* tss = &x86_get_percpu()->default_tss;
    tss->rsp0 = sp;
}

void x86_clear_tss_busy(seg_sel_t sel) {
    uint index = sel >> 3;
    struct task_desc* desc = (struct task_desc*)(_gdt + index * 8);
    desc->low &= ~TSS_DESC_BUSY_BIT;
}

void set_global_desc_64(seg_sel_t sel, uint64_t base, uint32_t limit,
                        uint8_t present, uint8_t ring, uint8_t sys,
                        uint8_t type, uint8_t gran, uint8_t bits) {
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

    struct seg_desc_64 entry = {};

    entry.limit_15_0 = limit & 0x0000ffff;
    entry.limit_19_16 = (limit & 0x000f0000) >> 16;

    entry.base_15_0 = base & 0x0000ffff;
    entry.base_23_16 = (base & 0x00ff0000) >> 16;
    entry.base_31_24 = (base & 0xff000000) >> 24;
    entry.base_63_32 = (uint32_t)(base >> 32);

    entry.type = type & 0x0f; // segment type
    entry.p = present != 0;   // present
    entry.dpl = ring & 0x03;  // descriptor privilege level
    entry.g = gran != 0;      // granularity
    entry.s = sys != 0;       // system / non-system
    entry.d_b = bits != 0;    // 16 / 32 bit

    // copy it into the appropriate entry
    uint index = sel >> 3;

    // for x86_64 index is still in units of 8 bytes into the gdt table
    struct seg_desc_64* g = (struct seg_desc_64*)(_gdt + index * 8);
    *g = entry;
}
