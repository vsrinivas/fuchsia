// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#pragma once

#include <assert.h>
#include <zircon/compiler.h>

__BEGIN_CDECLS

struct idt_entry {
    uint32_t w0, w1;
    uint32_t w2, w3;
};

struct idt {
    struct idt_entry entries[256];
};

static_assert(sizeof(struct idt_entry) == 16, "");
static_assert(sizeof(struct idt) == 16 * 256, "");

struct idtr {
    uint16_t limit;
    uintptr_t address;
} __PACKED;

enum idt_entry_type {
    IDT_INTERRUPT_GATE64 = 0xe,
    IDT_TRAP_GATE64 = 0xf,
};

enum idt_dpl {
    IDT_DPL0 = 0,
    IDT_DPL1 = 1,
    IDT_DPL2 = 2,
    IDT_DPL3 = 3,
};

/*
 * @brief Change an IDT entry
 *
 * Caution: Interrupts should probably be disabled when this is called
 *
 * @param idt Pointer to the IDT to change.
 * @param vec The vector to replace.
 * @param code_segment_sel The code segment selector to use on taking this
 *        interrupt.
 * @param entry_point_offset The offset of the code to begin executing (relative
 *        to the segment).
 * @param dpl The desired privilege level of the handler.
 * @param typ The type of interrupt handler
 */
void idt_set_vector(
        struct idt *idt,
        uint8_t vec,
        uint16_t code_segment_sel,
        uintptr_t entry_point_offset,
        enum idt_dpl dpl,
        enum idt_entry_type typ);

/*
 * @brief Set the Interrupt Stack Table index to use
 *
 * @param idt Pointer to the IDT to change.
 * @param vec The vector to change.
 * @param ist_idx A value in the range [0, 8) indicating which stack to use.
 *        If ist_idx == 0, use the normal stack for the target privilege level.
 */
void idt_set_ist_index(struct idt *idt, uint8_t vec, uint8_t ist_idx);

/*
 * @brief Initialize this IDT with our default values
 *
 * @param idt Pointer to the IDT to initialize
 */
void idt_setup(struct idt *idt);

/*
 * @brief Setup the read-only remapping of the IDT.
 */
void idt_setup_readonly(void);

/*
 * @brief Switch to thie given IDT
 *
 * @param idt Pointer to the IDT
 */
static void idt_load(struct idt *idt) {
    // After VM exit IDT limit is always set to 0xffff, so in order to avoid
    // calling LIDT in hypervisor to restore the proper IDT limit after every
    // VM exit in hypervisor we decided to use 0xffff all the time. There is
    // no harm in doing that because IDT limit is only relevant if it's smaller
    // than sizeof(struct idt) - 1 and doesn't affect anything otherwise.
    struct idtr idtr = { .limit = 0xffff, .address = (uintptr_t)idt };
    x86_lidt((uintptr_t)&idtr);
}

/*
 * @brief Get the read-only IDT.
 */
struct idt * idt_get_readonly(void);

__END_CDECLS
