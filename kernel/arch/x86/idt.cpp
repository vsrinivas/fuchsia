// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <assert.h>
#include <magenta/compiler.h>
#include <err.h>
#include <string.h>
#include <kernel/mp.h>
#include <vm/pmm.h>
#include <vm/vm_aspace.h>
#include <fbl/algorithm.h>
#include <arch/ops.h>

#include <arch/x86.h>
#include <arch/x86/descriptor.h>
#include <arch/x86/feature.h>
#include <arch/x86/idt.h>
#include <arch/x86/interrupts.h>

// The size of the `clac` instruction
#define CLAC_SIZE 3

// Early boot shared IDT structure
struct idt _idt_startup;
struct idtr _idtr = {
    .limit = sizeof(_idt_startup) - 1,
    .address =  (uintptr_t)&_idt_startup
};

// IDT after early boot
struct idt _idt __ALIGNED(PAGE_SIZE);
// Read-only remapping of the IDT
static struct idt *_idt_ro;

static inline void idt_set_segment_sel(struct idt_entry *entry, uint16_t sel)
{
    entry->w0 = (entry->w0 & 0x0000ffff) | (sel << 16);
}

static inline void idt_set_offset(struct idt_entry *entry, uintptr_t offset)
{
    uint32_t low_16 = offset & 0xffff;
    uint32_t mid_16 = (offset >> 16) & 0xffff;
    entry->w0 = (entry->w0 & 0xffff0000) | low_16;
    entry->w1 = (entry->w1 & 0x0000ffff) | (mid_16 << 16);
    uint32_t high_32 = (uint32_t)(offset >> 32);
    entry->w2 = high_32;
}

static inline void idt_set_present(struct idt_entry *entry, bool present)
{
    entry->w1 = (entry->w1 & ~(1 << 15)) | ((!!present) << 15);
}

static inline void idt_set_dpl(struct idt_entry *entry, enum idt_dpl dpl)
{
    ASSERT(dpl <= 3);
    entry->w1 = (entry->w1 & ~(3 << 13)) | ((uint32_t)dpl << 13);
}

static inline void idt_set_type(
        struct idt_entry *entry,
        enum idt_entry_type typ)
{
    entry->w1 = (entry->w1 & ~(0xf << 8)) | ((uint32_t)typ << 8);
}

void idt_set_vector(
        struct idt *idt,
        uint8_t vec,
        uint16_t code_segment_sel,
        uintptr_t entry_point_offset,
        enum idt_dpl dpl,
        enum idt_entry_type typ)
{
    struct idt_entry *entry = &idt->entries[vec];
    memset(entry, 0, sizeof(*entry));
    idt_set_segment_sel(entry, code_segment_sel);
    idt_set_offset(entry, entry_point_offset);
    idt_set_type(entry, typ);
    idt_set_dpl(entry, dpl);
    idt_set_present(entry, true);
}

void idt_set_ist_index(struct idt *idt, uint8_t vec, uint8_t ist_idx)
{
    ASSERT(ist_idx < 8);
    struct idt_entry *entry = &idt->entries[vec];
    entry->w1 = (entry->w1 & ~0x7) | ist_idx;
}

void idt_setup(struct idt *idt)
{
    extern uintptr_t const _isr_table[];

    // If SMAP is not available, we need to skip past the CLAC instruction
    // at the beginning of the ISR stubs.
    int clac_shift = 0;
    if (!x86_feature_test(X86_FEATURE_SMAP)) {
        clac_shift += CLAC_SIZE;
    }

    uint16_t sel;
    enum idt_entry_type typ;
    sel = CODE_64_SELECTOR;
    typ = IDT_INTERRUPT_GATE64;
    for (size_t i = 0; i < fbl::count_of(idt->entries); ++i) {
        uintptr_t offset = _isr_table[i] + clac_shift;
        enum idt_dpl dpl;
        switch (i) {
        case X86_INT_BREAKPOINT:
            dpl = IDT_DPL3;
            break;
        default:
            dpl = IDT_DPL0;
            break;
        }
        idt_set_vector(idt, (uint8_t)i, sel, offset, dpl, typ);
    }
}

// Create a read-only remapping of the global IDT.
// This function is called on arch initialization before additional cpus
// started. It reloads the main processor IDT to be read-only. Each
// additional cpu will pick-up the read-only IDT by default.
// TODO(thgarnie): Move to C++ and non-compact VMAR for KASLR support.
void idt_setup_readonly(void) {
    DEBUG_ASSERT(arch_curr_cpu_num() == 0);
    DEBUG_ASSERT(mp_get_online_mask() == 1);
    status_t status = VmAspace::kernel_aspace()->AllocPhysical(
                                         "idt_readonly",
                                         sizeof(_idt),
                                         (void **)&_idt_ro,
                                         PAGE_SIZE_SHIFT,
                                         vaddr_to_paddr(&_idt),
                                         0 /* vmm flags */,
                                         ARCH_MMU_FLAG_PERM_READ);
    ASSERT(status == MX_OK);
    idt_load(_idt_ro);
}

// Get the read-only IDT
struct idt * idt_get_readonly(void) {
    ASSERT(_idt_ro != NULL);
    return _idt_ro;
}
