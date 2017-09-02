// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <assert.h>

#include <err.h>
#include <trace.h>
#include <arch/x86/apic.h>
#include <arch/x86/interrupts.h>
#include <kernel/spinlock.h>
#include <vm/pmm.h>
#include <vm/vm_aspace.h>

#define IO_APIC_IND(base) ((volatile uint32_t *)(((uint8_t *)(base)) + IO_APIC_IOREGSEL))
#define IO_APIC_DAT(base) ((volatile uint32_t *)(((uint8_t *)(base)) + IO_APIC_IOWIN))
#define IO_APIC_EOIR(base) ((volatile uint32_t *)(((uint8_t *)(base)) + 0x40))
// The minimum address space required past the base address
#define IO_APIC_WINDOW_SIZE 0x44
// The minimum version that supported the EOIR
#define IO_APIC_EOIR_MIN_VERSION 0x20

// IO APIC register offsets
#define IO_APIC_REG_RTE(idx) (0x10 + 2 * (idx))

// Macros for extracting data from REG_ID
#define IO_APIC_ID_ID(v) (((v) >> 24) & 0xf)
// Macros for extracting data from REG_VER
#define IO_APIC_VER_MAX_REDIR_ENTRY(v) (((v) >> 16) & 0xff)
#define IO_APIC_VER_VERSION(v) ((v) & 0xff)
// Macros for writing REG_RTE entries
#define IO_APIC_RTE_DST(v) (((uint64_t)(v)) << 56)
#define IO_APIC_RTE_EXTENDED_DST_ID(v) (((uint64_t)((v) & 0xf)) << 48)
#define IO_APIC_RTE_MASKED (1ULL << 16)
#define IO_APIC_RTE_TRIGGER_MODE(tm) (((uint64_t)(tm)) << 15)
#define IO_APIC_RTE_POLARITY(p) (((uint64_t)(p)) << 13)
#define IO_APIC_RTE_DST_MODE(dm) (((uint64_t)(dm)) << 11)
#define IO_APIC_RTE_DELIVERY_MODE(dm) ((((uint64_t)(dm)) & 0x7) << 8)
#define IO_APIC_RTE_VECTOR(x) (((uint64_t)(x)) & 0xff)
#define IO_APIC_RTE_MASK IO_APIC_RTE_VECTOR(0xff)
// Macros for reading REG_RTE entries
#define IO_APIC_RTE_REMOTE_IRR (1ULL << 14)
#define IO_APIC_RTE_DELIVERY_STATUS (1ULL << 12)
#define IO_APIC_RTE_GET_POLARITY(r) \
        ((enum interrupt_polarity)(((r) >> 13) & 0x1))
#define IO_APIC_RTE_GET_TRIGGER_MODE(r) \
        ((enum interrupt_trigger_mode)(((r) >> 15) & 0x1))
#define IO_APIC_RTE_GET_VECTOR(r) \
        ((uint8_t)((r) & 0xFF))

// Technically this can be larger, but the spec as of the 100-Series doesn't
// guarantee where the additional redirections will be.
#define IO_APIC_NUM_REDIRECTIONS 120


#define LOCAL_TRACE 0

// Struct for tracking all we need to know about each IO APIC
struct io_apic {
    struct io_apic_descriptor desc;

    // Virtual address of the base of this IOAPIC's MMIO
    void *vaddr;

    uint8_t version;
    // The index of the last redirection entry
    uint8_t max_redirection_entry;
};

// General register accessors
static inline uint32_t apic_io_read_reg(
        struct io_apic *io_apic,
        uint8_t reg);
static inline void apic_io_write_reg(
        struct io_apic *io_apic,
        uint8_t reg,
        uint32_t val);

// Register-specific accessors
static uint64_t apic_io_read_redirection_entry(
        struct io_apic *io_apic,
        uint32_t global_irq);
static void apic_io_write_redirection_entry(
        struct io_apic *io_apic,
        uint32_t global_irq,
        uint64_t value);

// Utility for finding the right IO APIC for a specific global IRQ, cannot fail
static struct io_apic *apic_io_resolve_global_irq(uint32_t irq);
// Utility for finding the right IO APIC for a specific global IRQ, can fail
static struct io_apic *apic_io_resolve_global_irq_no_panic(uint32_t irq);

// This lock guards all access to IO APIC registers
static spin_lock_t lock = SPIN_LOCK_INITIAL_VALUE;

// Track all IO APICs in the system
static struct io_apic *io_apics;
static uint32_t num_io_apics;

// The first 16 global IRQs are identity mapped to the legacy ISA IRQs unless
// we are told otherwise.  This tracks the actual mapping.
// Read-only after initialization in apic_io_init()
static struct io_apic_isa_override isa_overrides[NUM_ISA_IRQS];

void apic_io_init(
        struct io_apic_descriptor *io_apic_descs,
        unsigned int num_io_apic_descs,
        struct io_apic_isa_override *overrides,
        unsigned int num_overrides)
{
    ASSERT(io_apics == NULL);

    num_io_apics = num_io_apic_descs;
    io_apics = (io_apic *)calloc(num_io_apics, sizeof(*io_apics));
    ASSERT(io_apics != NULL);
    for (unsigned int i = 0; i < num_io_apics; ++i) {
        io_apics[i].desc = io_apic_descs[i];
    }

    // Allocate windows to their control pages
    for (uint32_t i = 0; i < num_io_apics; ++i) {
        struct io_apic *apic = &io_apics[i];
        paddr_t paddr = apic->desc.paddr;
        void *vaddr = paddr_to_kvaddr(paddr);
        // If the window isn't mapped yet (multiple IO APICs can be in the
        // same page), map it in.
        if (vaddr == NULL) {
            paddr_t paddr_page_base = ROUNDDOWN(paddr, PAGE_SIZE);
            ASSERT(paddr + IO_APIC_WINDOW_SIZE <= paddr_page_base + PAGE_SIZE);
            status_t res = VmAspace::kernel_aspace()->AllocPhysical(
                    "ioapic",
                    PAGE_SIZE, // size
                    &vaddr, // requested virtual vaddress
                    PAGE_SIZE_SHIFT, // alignment log2
                    paddr_page_base, // physical vaddress
                    0, // vmm flags
                    ARCH_MMU_FLAG_PERM_READ | ARCH_MMU_FLAG_PERM_WRITE |
                        ARCH_MMU_FLAG_UNCACHED_DEVICE); // arch mmu flags
            ASSERT(res == MX_OK);
            vaddr = (void *)((uintptr_t)vaddr + paddr - paddr_page_base);
        }

        // Populate the rest of the descriptor
        apic->vaddr = vaddr;

        spin_lock_saved_state_t state;
        spin_lock_irqsave(&lock, state);
        uint32_t ver = apic_io_read_reg(apic, IO_APIC_REG_VER);

        apic->version = IO_APIC_VER_VERSION(ver);
        apic->max_redirection_entry = IO_APIC_VER_MAX_REDIR_ENTRY(ver);
        LTRACEF("Found an IO APIC at phys %p, virt %p: ver %08x\n", (void *)paddr, vaddr, ver);
        if (apic->max_redirection_entry > IO_APIC_NUM_REDIRECTIONS - 1) {
            TRACEF("IO APIC supports more redirections than kernel: %08x\n",
                   ver);
            apic->max_redirection_entry = IO_APIC_NUM_REDIRECTIONS - 1;
        }

        // Cleanout the redirection entries
        for (unsigned int j = 0; j <= apic->max_redirection_entry; ++j) {
            apic_io_write_redirection_entry(
                    apic, j + apic->desc.global_irq_base, IO_APIC_RTE_MASKED);
        }
        spin_unlock_irqrestore(&lock, state);
    }

    // Process ISA IRQ overrides
    for (unsigned int i = 0; i < num_overrides; ++i) {
        uint8_t isa_irq = overrides[i].isa_irq;
        ASSERT(isa_irq < NUM_ISA_IRQS);
        isa_overrides[isa_irq] = overrides[i];
        LTRACEF("ISA IRQ override for ISA IRQ %u, mapping to %u\n",
                isa_irq, overrides[i].global_irq);
    }
}

static struct io_apic *apic_io_resolve_global_irq_no_panic(uint32_t irq)
{
    for (uint32_t i = 0; i < num_io_apics; ++i) {
        uint32_t start = io_apics[i].desc.global_irq_base;
        uint32_t end = start + io_apics[i].max_redirection_entry;
        if (start <= irq && irq <= end) {
            return &io_apics[i];
        }
    }
    return NULL;
}

static struct io_apic *apic_io_resolve_global_irq(uint32_t irq)
{
    struct io_apic *res = apic_io_resolve_global_irq_no_panic(irq);
    if (res) {
        return res;
    }
    // Treat this as fatal, since dealing with an unmapped IRQ is a bug.
    panic("Could not resolve global IRQ: %u\n", irq);
}

static inline uint32_t apic_io_read_reg(
        struct io_apic *io_apic,
        uint8_t reg)
{
    ASSERT(io_apic != NULL);
    DEBUG_ASSERT(spin_lock_held(&lock));
    *IO_APIC_IND(io_apic->vaddr) = reg;
    uint32_t val = *IO_APIC_DAT(io_apic->vaddr);
    return val;
}

static inline void apic_io_write_reg(
        struct io_apic *io_apic,
        uint8_t reg,
        uint32_t val)
{
    ASSERT(io_apic != NULL);
    DEBUG_ASSERT(spin_lock_held(&lock));
    *IO_APIC_IND(io_apic->vaddr) = reg;
    *IO_APIC_DAT(io_apic->vaddr) = val;
}

static uint64_t apic_io_read_redirection_entry(
        struct io_apic *io_apic,
        uint32_t global_irq)
{
    DEBUG_ASSERT(spin_lock_held(&lock));

    ASSERT(global_irq >= io_apic->desc.global_irq_base);
    uint32_t offset = global_irq - io_apic->desc.global_irq_base;
    ASSERT(offset <= io_apic->max_redirection_entry);

    uint8_t reg_id = (uint8_t)IO_APIC_REG_RTE(offset);
    uint64_t result = 0;
    result |= apic_io_read_reg(io_apic, reg_id);
    result |= ((uint64_t)apic_io_read_reg(io_apic, (uint8_t)(reg_id + 1))) << 32;
    return result;
}

static void apic_io_write_redirection_entry(
        struct io_apic *io_apic,
        uint32_t global_irq,
        uint64_t value)
{
    DEBUG_ASSERT(spin_lock_held(&lock));

    ASSERT(global_irq >= io_apic->desc.global_irq_base);
    uint32_t offset = global_irq - io_apic->desc.global_irq_base;
    ASSERT(offset <= io_apic->max_redirection_entry);

    uint8_t reg_id = (uint8_t)IO_APIC_REG_RTE(offset);
    apic_io_write_reg(io_apic, reg_id, (uint32_t)value);
    apic_io_write_reg(io_apic, (uint8_t)(reg_id + 1), (uint32_t)(value >> 32));
}

bool apic_io_is_valid_irq(uint32_t global_irq)
{
    return apic_io_resolve_global_irq_no_panic(global_irq) != NULL;
}

/*
 * To correctly use this function, we need to do some work first.
 * 1) We need to check for EOI-broadcast suppression support in the local APIC
 *    version register.
 * 2) We need to check that the IOAPIC is new enough to support the EOI
 * 3) We need to enable suppression in the spurious interrupt register.
 * 4) Call this function after calling apic_issue_eoi() (or maybe modify
 *    apic_issue_eoi() to call this automatically).
 *
 * In the mean time, IO APIC EOIs are automatically issued via broadcast to
 * all IO APICs whenever the local APIC receives an EOI for a level-triggered
 * interrupt.
 */
void apic_io_issue_eoi(uint32_t global_irq, uint8_t vec)
{
    struct io_apic *io_apic = apic_io_resolve_global_irq(global_irq);

    spin_lock_saved_state_t state;
    spin_lock_irqsave(&lock, state);

    ASSERT(io_apic->version >= IO_APIC_EOIR_MIN_VERSION);
    *IO_APIC_EOIR(io_apic->vaddr) = vec;

    spin_unlock_irqrestore(&lock, state);
}

void apic_io_mask_irq(uint32_t global_irq, bool mask)
{
    struct io_apic *io_apic = apic_io_resolve_global_irq(global_irq);

    spin_lock_saved_state_t state;
    spin_lock_irqsave(&lock, state);

    uint64_t reg = apic_io_read_redirection_entry(io_apic, global_irq);
    if (mask) {
        reg |= IO_APIC_RTE_MASKED;
    } else {
        /* If we are unmasking, we had better have been assigned a valid vector */
        DEBUG_ASSERT((IO_APIC_RTE_GET_VECTOR(reg) >= X86_INT_PLATFORM_BASE) &&
                     (IO_APIC_RTE_GET_VECTOR(reg) <= X86_INT_PLATFORM_MAX));
        reg &= ~IO_APIC_RTE_MASKED;
    }
    apic_io_write_redirection_entry(io_apic, global_irq, reg);

    spin_unlock_irqrestore(&lock, state);
}

void apic_io_configure_irq(
        uint32_t global_irq,
        enum interrupt_trigger_mode trig_mode,
        enum interrupt_polarity polarity,
        enum apic_interrupt_delivery_mode del_mode,
        bool mask,
        enum apic_interrupt_dst_mode dst_mode,
        uint8_t dst,
        uint8_t vector)
{
    struct io_apic *io_apic = apic_io_resolve_global_irq(global_irq);

    spin_lock_saved_state_t state;
    spin_lock_irqsave(&lock, state);

    /* If we are configuring an invalid vector, for the IRQ to be masked. */
    if ((del_mode == DELIVERY_MODE_FIXED || del_mode == DELIVERY_MODE_LOWEST_PRI) &&
        ((vector < X86_INT_PLATFORM_BASE) || (vector > X86_INT_PLATFORM_MAX))) {

        mask = true;
    }

    uint64_t reg = 0;
    reg |= IO_APIC_RTE_TRIGGER_MODE(trig_mode);
    reg |= IO_APIC_RTE_POLARITY(polarity);
    reg |= IO_APIC_RTE_DELIVERY_MODE(del_mode);
    reg |= IO_APIC_RTE_DST_MODE(dst_mode);
    reg |= IO_APIC_RTE_DST(dst);
    reg |= IO_APIC_RTE_VECTOR(vector);
    if (mask) {
        reg |= IO_APIC_RTE_MASKED;
    }
    apic_io_write_redirection_entry(io_apic, global_irq, reg);

    spin_unlock_irqrestore(&lock, state);
}

status_t apic_io_fetch_irq_config(
        uint32_t global_irq,
        enum interrupt_trigger_mode* trig_mode,
        enum interrupt_polarity* polarity)
{
    struct io_apic *io_apic = apic_io_resolve_global_irq(global_irq);

    if (!io_apic)
        return MX_ERR_INVALID_ARGS;

    spin_lock_saved_state_t state;
    spin_lock_irqsave(&lock, state);

    uint64_t reg = apic_io_read_redirection_entry(io_apic, global_irq);
    if (trig_mode) *trig_mode = IO_APIC_RTE_GET_TRIGGER_MODE(reg);
    if (polarity)  *polarity  = IO_APIC_RTE_GET_POLARITY(reg);

    spin_unlock_irqrestore(&lock, state);

    return MX_OK;
}

void apic_io_configure_irq_vector(
        uint32_t global_irq,
        uint8_t vector)
{
    struct io_apic *io_apic = apic_io_resolve_global_irq(global_irq);

    spin_lock_saved_state_t state;
    spin_lock_irqsave(&lock, state);

    uint64_t reg = apic_io_read_redirection_entry(io_apic, global_irq);

    /* If we are configuring an invalid vector, automatically mask the IRQ. */
    if ((IO_APIC_RTE_GET_VECTOR(reg) < X86_INT_PLATFORM_BASE) ||
        (IO_APIC_RTE_GET_VECTOR(reg) > X86_INT_PLATFORM_MAX)) {
        reg |= IO_APIC_RTE_MASKED;
    }

    reg &= ~IO_APIC_RTE_MASK;
    reg |= IO_APIC_RTE_VECTOR(vector);
    apic_io_write_redirection_entry(io_apic, global_irq, reg);

    spin_unlock_irqrestore(&lock, state);
}

uint8_t apic_io_fetch_irq_vector(uint32_t global_irq)
{
    struct io_apic *io_apic = apic_io_resolve_global_irq(global_irq);

    spin_lock_saved_state_t state;
    spin_lock_irqsave(&lock, state);

    uint64_t reg = apic_io_read_redirection_entry(io_apic, global_irq);
    uint8_t  vector = IO_APIC_RTE_GET_VECTOR(reg);

    spin_unlock_irqrestore(&lock, state);

    return vector;
}

void apic_io_mask_isa_irq(uint8_t isa_irq, bool mask)
{
    ASSERT(isa_irq < NUM_ISA_IRQS);
    uint32_t global_irq = isa_irq;
    if (isa_overrides[isa_irq].remapped) {
        global_irq = isa_overrides[isa_irq].global_irq;
    }
    apic_io_mask_irq(global_irq, mask);
}

void apic_io_configure_isa_irq(
        uint8_t isa_irq,
        enum apic_interrupt_delivery_mode del_mode,
        bool mask,
        enum apic_interrupt_dst_mode dst_mode,
        uint8_t dst,
        uint8_t vector)
{
    ASSERT(isa_irq < NUM_ISA_IRQS);
    uint32_t global_irq = isa_irq;
    enum interrupt_trigger_mode trig_mode = IRQ_TRIGGER_MODE_EDGE;
    enum interrupt_polarity polarity = IRQ_POLARITY_ACTIVE_HIGH;
    if (isa_overrides[isa_irq].remapped) {
        global_irq = isa_overrides[isa_irq].global_irq;
        trig_mode = isa_overrides[isa_irq].tm;
        polarity = isa_overrides[isa_irq].pol;
    }

    apic_io_configure_irq(
            global_irq,
            trig_mode,
            polarity,
            del_mode,
            mask,
            dst_mode,
            dst,
            vector);
}

// Convert a legacy ISA IRQ number into a global IRQ number
uint32_t apic_io_isa_to_global(uint8_t isa_irq)
{
    // It is a programming bug for this to be invoked with an invalid value.
    ASSERT(isa_irq < NUM_ISA_IRQS);
    if (isa_overrides[isa_irq].remapped) {
        return isa_overrides[isa_irq].global_irq;
    }
    return isa_irq;
}

void apic_io_debug(void)
{
    spin_lock_saved_state_t state;
    spin_lock_irqsave(&lock, state);
    for (uint32_t i = 0; i < num_io_apics; ++i) {
        struct io_apic *apic = &io_apics[i];
        printf("IO APIC idx %u:\n", i);
        printf("  id: %08x\n", apic->desc.apic_id);
        printf("  version: %08hhx\n", apic->version);
        printf("  entries: %08x\n", apic->max_redirection_entry + 1U);
        for (uint8_t j = 0; j <= apic->max_redirection_entry; ++j) {
            uint32_t global_irq = apic->desc.global_irq_base + j;
            uint64_t reg = apic_io_read_redirection_entry(apic, global_irq);
            printf("    %4u: dst: %s %02hhx, %s, %s, %s, dm %hhx, vec %2hhx, %s %s\n",
                   global_irq,
                   (reg & (1 << 11)) ? "l" : "p",
                   (uint8_t)(reg >> 56),
                   (reg & IO_APIC_RTE_MASKED) ? "masked" : "unmasked",
                   IO_APIC_RTE_GET_TRIGGER_MODE(reg) ? "level" : "edge",
                   IO_APIC_RTE_GET_POLARITY(reg) ? "low" : "high",
                   (uint8_t)((reg >> 8) & 0x7),
                   (uint8_t)reg,
                   (reg & (1 << 12)) ? "pending" : "",
                   (reg & (1 << 14)) ? "RIRR" : "");
        }
    }
    spin_unlock_irqrestore(&lock, state);
}
