// Copyright 2016 The Fuchsia Authors
// Copyright (c) 2015 Travis Geiselbrecht
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT
#include <assert.h>
#include <bits.h>
#include <dev/interrupt.h>
#include <err.h>
#include <kernel/mp.h>
#include <kernel/spinlock.h>
#include <kernel/thread.h>
#include <dev/bcm28xx.h>
#include <trace.h>
#include <arch/arm64.h>

#include <mdi/mdi.h>
#include <mdi/mdi-defs.h>
#include <pdev/driver.h>
#include <pdev/interrupt.h>

#define LOCAL_TRACE 0

static spin_lock_t lock = SPIN_LOCK_INITIAL_VALUE;

static status_t bcm28xx_mask_interrupt(unsigned int vector) {
    LTRACEF("vector %u\n", vector);

    spin_lock_saved_state_t state;
    spin_lock_irqsave(&lock, state);

    if (vector >= INTERRUPT_ARM_LOCAL_CNTPSIRQ && vector <= INTERRUPT_ARM_LOCAL_CNTVIRQ) {
        // local timer interrupts, mask on all cpus
        for (uint cpu = 0; cpu < 4; cpu++) {
            uintptr_t reg = INTC_LOCAL_TIMER_INT_CONTROL0 + cpu * 4;

            *REG32(reg) &= (1 << (vector - INTERRUPT_ARM_LOCAL_CNTPSIRQ));
        }
    } else if (/* vector >= ARM_IRQ1_BASE && */ vector < (ARM_IRQ0_BASE + 32)) {
        uintptr_t reg;
        if (vector >= ARM_IRQ0_BASE)
            reg = INTC_DISABLE3;
        else if (vector >= ARM_IRQ2_BASE)
            reg = INTC_DISABLE2;
        else
            reg = INTC_DISABLE1;

        *REG32(reg) = 1 << (vector % 32);
    } else if ( vector >= INTERRUPT_ARM_LOCAL_MAILBOX0 && vector <= INTERRUPT_ARM_LOCAL_MAILBOX3) {
        for (uint cpu = 0; cpu < 4; cpu++) {
            uintptr_t reg = INTC_LOCAL_MAILBOX_INT_CONTROL0 + cpu * 4;
            *REG32(reg) &= ~(1 << (vector - INTERRUPT_ARM_LOCAL_MAILBOX0));
        }
    } else {
        PANIC_UNIMPLEMENTED;
    }

    spin_unlock_irqrestore(&lock, state);

    return NO_ERROR;
}

static status_t bcm28xx_unmask_interrupt(unsigned int vector) {
    LTRACEF("vector %u\n", vector);

    spin_lock_saved_state_t state;
    spin_lock_irqsave(&lock, state);

    if (vector >= INTERRUPT_ARM_LOCAL_CNTPSIRQ && vector <= INTERRUPT_ARM_LOCAL_CNTVIRQ) {
        // local timer interrupts, unmask for all cpus
        for (uint cpu = 0; cpu < 4; cpu++) {
            uintptr_t reg = INTC_LOCAL_TIMER_INT_CONTROL0 + cpu * 4;

            *REG32(reg) |= (1 << (vector - INTERRUPT_ARM_LOCAL_CNTPSIRQ));
        }
    } else if (/* vector >= ARM_IRQ1_BASE && */ vector < (ARM_IRQ0_BASE + 32)) {
        uintptr_t reg;
        if (vector >= ARM_IRQ0_BASE)
            reg = INTC_ENABLE3;
        else if (vector >= ARM_IRQ2_BASE)
            reg = INTC_ENABLE2;
        else
            reg = INTC_ENABLE1;
        //printf("vector = %x   reg=%lx\n",vector,reg);
        //printf("basic pending = %08x\n", *(uint32_t *)0xffffffffc000b200);
        //printf("irq1  pending = %08x\n", *(uint32_t *)0xffffffffc000b204);
        //printf("irq2  pending = %08x\n", *(uint32_t *)0xffffffffc000b208);
        *REG32(reg) = 1 << (vector % 32);
    } else if ( vector >= INTERRUPT_ARM_LOCAL_MAILBOX0 && vector <= INTERRUPT_ARM_LOCAL_MAILBOX3) {
        for (uint cpu = 0; cpu < 4; cpu++) {
            uintptr_t reg = INTC_LOCAL_MAILBOX_INT_CONTROL0 + cpu * 4;
            *REG32(reg) |= 1 << (vector - INTERRUPT_ARM_LOCAL_MAILBOX0);
        }
    } else {
        PANIC_UNIMPLEMENTED;
    }

    spin_unlock_irqrestore(&lock, state);

    return NO_ERROR;
}

static bool bcm28xx_is_valid_interrupt(unsigned int vector, uint32_t flags) {
    return (vector < MAX_INT);
}

static unsigned int bcm28xx_remap_interrupt(unsigned int vector) {
    return vector;
}

/*
 *  TODO(hollande) - Implement!
 */
static status_t bcm28xx_configure_interrupt(unsigned int vector,
                                            enum interrupt_trigger_mode tm,
                                            enum interrupt_polarity pol)
{
    return NO_ERROR;
}

/*
 *  TODO(hollande) - Implement!
 */
static status_t bcm28xx_get_interrupt_config(unsigned int vector,
                                             enum interrupt_trigger_mode* tm,
                                             enum interrupt_polarity* pol)
{
    if (tm)  *tm  = IRQ_TRIGGER_MODE_EDGE;
    if (pol) *pol = IRQ_POLARITY_ACTIVE_HIGH;
    return NO_ERROR;
}

static enum handler_return bcm28xx_handle_irq(struct arm64_iframe_short* frame) {
    uint vector;
    uint cpu = arch_curr_cpu_num();

    // see what kind of irq it is
    uint32_t pend = *REG32(INTC_LOCAL_IRQ_PEND0 + cpu * 4);

    pend &= ~(1 << (INTERRUPT_ARM_LOCAL_GPU_FAST % 32)); // mask out gpu interrupts

    if (pend != 0) {
        // it's a local interrupt
        LTRACEF("local pend 0x%x\n", pend);
        vector = ARM_IRQ_LOCAL_BASE + ctz(pend);
        goto decoded;
    }

// XXX disable for now, since all of the interesting irqs are mirrored into the other banks
#if 0
    // look in bank 0 (ARM interrupts)
    pend = *REG32(INTC_PEND0);
    LTRACEF("pend0 0x%x\n", pend);
    pend &= ~((1<<8)|(1<<9)); // mask out bit 8 and 9
    if (pend != 0) {
        // it's a bank 0 interrupt
        vector = ARM_IRQ0_BASE + ctz(pend);
        goto decoded;
    }
#endif

    // look for VC interrupt bank 1
    pend = *REG32(INTC_PEND1);
    LTRACEF("pend1 0x%x\n", pend);
    if (pend != 0) {
        // it's a bank 1 interrupt
        vector = ARM_IRQ1_BASE + ctz(pend);
        goto decoded;
    }

    // look for VC interrupt bank 2
    pend = *REG32(INTC_PEND2);
    LTRACEF("pend2 0x%x\n", pend);
    if (pend != 0) {
        // it's a bank 2 interrupt
        vector = ARM_IRQ2_BASE + ctz(pend);
        goto decoded;
    }

    vector = 0xffffffff;

decoded:
    LTRACEF("cpu %u vector %u\n", cpu, vector);

    // dispatch the irq
    enum handler_return ret = INT_NO_RESCHEDULE;

#if WITH_SMP
    if (vector == INTERRUPT_ARM_LOCAL_MAILBOX0) {
        pend = *REG32(INTC_LOCAL_MAILBOX0_CLR0 + 0x10 * cpu);
        LTRACEF("mailbox0 clr 0x%x\n", pend);

        // ack it
        *REG32(INTC_LOCAL_MAILBOX0_CLR0 + 0x10 * cpu) = pend;

        if (pend & (1 << MP_IPI_GENERIC)) {
            ret = mp_mbx_generic_irq();
        }
        if (pend & (1 << MP_IPI_RESCHEDULE)) {
            ret = mp_mbx_reschedule_irq();
        }
    } else
#endif // WITH_SMP
    if (vector == 0xffffffff) {
        ret = INT_NO_RESCHEDULE;
    } else {
        struct int_handler_struct* handler = pdev_get_int_handler(vector);
        if (handler && handler->handler) {
            if (vector < ARM_IRQ_LOCAL_BASE) {
                THREAD_STATS_INC(interrupts);
            }
            ret = handler->handler(handler->arg);
        } else {
            panic("irq %u fired on cpu %u but no handler set!\n", vector, cpu);
        }
    }

    return ret;
}

static enum handler_return bcm28xx_handle_fiq(struct arm64_iframe_short* frame) {
    PANIC_UNIMPLEMENTED;
}


static status_t bcm28xx_send_ipi(mp_cpu_mask_t target, mp_ipi_t ipi) {
    /* filter out targets outside of the range of cpus we care about */
    target &= ((1UL << SMP_MAX_CPUS) - 1);
    if (target != 0) {
        LTRACEF("ipi %u, target 0x%x\n", ipi, target);

        for (uint i = 0; i < 4; i++) {
            if (target & (1 << i)) {
                LTRACEF("sending to cpu %u\n", i);
                *REG32(INTC_LOCAL_MAILBOX0_SET0 + 0x10 * i) = (1 << ipi);
            }
        }
    }

    return NO_ERROR;
}

static void bcm28xx_init_percpu_early(void) {
}

static void bcm28xx_shutdown(void) {
    // Mask away all interrupts.
    *REG32(INTC_DISABLE1) = 0xffffffff;
    *REG32(INTC_DISABLE2) = 0xffffffff;
    *REG32(INTC_DISABLE3) = 0xffffffff;
}

static void bcm28xx_init_percpu(void) {
    mp_set_curr_cpu_online(true);
    bcm28xx_unmask_interrupt(INTERRUPT_ARM_LOCAL_MAILBOX0);
}

static const struct pdev_interrupt_ops intc_ops = {
    .mask = bcm28xx_mask_interrupt,
    .unmask = bcm28xx_unmask_interrupt,
    .configure = bcm28xx_configure_interrupt,
    .get_config = bcm28xx_get_interrupt_config,
    .is_valid = bcm28xx_is_valid_interrupt,
    .remap = bcm28xx_remap_interrupt,
    .send_ipi = bcm28xx_send_ipi,
    .init_percpu_early = bcm28xx_init_percpu_early,
    .init_percpu = bcm28xx_init_percpu,
    .handle_irq = bcm28xx_handle_irq,
    .handle_fiq = bcm28xx_handle_fiq,
    .shutdown = bcm28xx_shutdown,
};

static void bcm28xx_intc_init(mdi_node_ref_t* node, uint level) {
    // nothing to read from MDI, so arguments are ignored

    // mask everything
    *REG32(INTC_DISABLE1) = 0xffffffff;
    *REG32(INTC_DISABLE2) = 0xffffffff;
    *REG32(INTC_DISABLE3) = 0xffffffff;

    pdev_register_interrupts(&intc_ops);
}

LK_PDEV_INIT(bcm28xx_intc_init, MDI_KERNEL_DRIVERS_BCM28XX_INTERRUPT, bcm28xx_intc_init, LK_INIT_LEVEL_PLATFORM_EARLY);
