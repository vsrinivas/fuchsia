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
#include <kernel/mp.h>
#include <string.h>
#include <trace.h>
#include <vm/fault.h>
#include <vm/pmm.h>
#include <vm/vm_aspace.h>
#include <vm/vm_object_paged.h>
#include <vm/vm_object_physical.h>
#include <zircon/compiler.h>

#define TSS_DESC_BUSY_BIT (1ull << 41)

/* Temporary GDT defined in assembly is used during AP/BP setup process */
extern uint8_t _temp_gdt[];
extern uint8_t _temp_gdt_end[];

/* We create a new GDT after initialization is done and switch everyone to it */
static uintptr_t gdt = (uintptr_t)_temp_gdt;

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
    struct task_desc* desc = (struct task_desc*)(gdt + index * 8);
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
    struct seg_desc_64* g = (struct seg_desc_64*)(gdt + index * 8);
    *g = entry;
}

void gdt_setup() {
    DEBUG_ASSERT(arch_curr_cpu_num() == 0);
    DEBUG_ASSERT(mp_get_online_mask() == 1);
    // Max GDT size is limited to 64K and we reserve the whole 64K area, but we map
    // just enough pages to store GDT and leave the rest unmapped so all accesses
    // beyond GDT last page are going to cause page fault.
    // Why don't we just set a a proper limit value? That's because during VM exit
    // on x86 architecture GDT limit is always set to 0xFFFF (see Intel SDM, Volume
    // 3, 27.5.2 Loading Host Segment and Descriptor-Table Registers) and therefore
    // requiring the hypervisor to restore GDT limit after VM exit using LGDT
    // instruction which is a serializing instruction (see Intel SDM, Volume 3, 8.3
    // Serializing Instructions).
    uint32_t vmar_flags = VMAR_FLAG_CAN_MAP_SPECIFIC | VMAR_FLAG_CAN_MAP_READ | VMAR_FLAG_CAN_MAP_WRITE;
    uint32_t mmu_flags = ARCH_MMU_FLAG_PERM_READ | ARCH_MMU_FLAG_PERM_WRITE;

    size_t gdt_real_size = _temp_gdt_end - _temp_gdt;
    size_t gdt_size = 0x10000;

    fbl::RefPtr<VmObject> vmo;
    zx_status_t status = VmObjectPaged::Create(PMM_ALLOC_FLAG_ANY, /*options*/0u, gdt_real_size, &vmo);
    ASSERT(status == ZX_OK);

    fbl::RefPtr<VmAddressRegion> vmar;
    status = VmAspace::kernel_aspace()->RootVmar()->CreateSubVmar(
        0, gdt_size, PAGE_SIZE_SHIFT, vmar_flags, "gdt_vmar", &vmar);
    ASSERT(status == ZX_OK);

    fbl::RefPtr<VmMapping> mapping;
    status = vmar->CreateVmMapping(
        /*mapping_offset*/0u, gdt_real_size, PAGE_SIZE_SHIFT, VMAR_FLAG_SPECIFIC, fbl::move(vmo),
        /*vmo_offset*/0u, mmu_flags, "gdt", &mapping);
    ASSERT(status == ZX_OK);

    status = mapping->MapRange(0, gdt_real_size, /*commit*/true);
    ASSERT(status == ZX_OK);

    memcpy((void*)mapping->base(), _temp_gdt, gdt_real_size);
    gdt = mapping->base();
    gdt_load(gdt_get());
}

uintptr_t gdt_get(void) {
    return gdt;
}
