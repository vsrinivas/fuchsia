// Copyright 2017 The Fuchsia Authors
// Copyright (c) 2017, Google Inc. All rights reserved.
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <assert.h>
#include <err.h>
#include <inttypes.h>
#include <string.h>
#include <dev/interrupt/arm_gic.h>
#include <dev/interrupt/arm_gicv3_regs.h>
#include <kernel/thread.h>
#include <lk/init.h>
#include <dev/interrupt.h>
#include <trace.h>
#include <lib/ktrace.h>

#define LOCAL_TRACE 0

#include <arch/arm64.h>
#define iframe arm64_iframe_short
#define IFRAME_PC(frame) ((frame)->elr)

static spin_lock_t gicd_lock;
#define GICD_LOCK_FLAGS SPIN_LOCK_FLAG_INTERRUPTS
#define GIC_MAX_PER_CPU_INT 32

#include <arch/arch_ops.h>

#ifndef GIC_IPI_BASE
#define GIC_IPI_BASE (16 - MAX_IPI)
#endif

struct int_handler_struct {
    int_handler handler;
    void* arg;
};

static bool arm_gic_interrupt_change_allowed(int irq)
{
    return true;
}

static struct int_handler_struct int_handler_table_per_cpu[GIC_MAX_PER_CPU_INT][SMP_MAX_CPUS];
static struct int_handler_struct int_handler_table_shared[MAX_INT-GIC_MAX_PER_CPU_INT];

static struct int_handler_struct* get_int_handler(unsigned int vector, uint cpu)
{
    if (vector < GIC_MAX_PER_CPU_INT) {
        return &int_handler_table_per_cpu[vector][cpu];
    } else {
        return &int_handler_table_shared[vector - GIC_MAX_PER_CPU_INT];
    }
}

void register_int_handler(unsigned int vector, int_handler handler, void* arg)
{
    struct int_handler_struct *h;
    uint cpu = arch_curr_cpu_num();

    spin_lock_saved_state_t state;

    if (vector >= MAX_INT) {
        panic("register_int_handler: vector out of range %u\n", vector);
    }

    spin_lock_save(&gicd_lock, &state, GICD_LOCK_FLAGS);

    if (arm_gic_interrupt_change_allowed(vector)) {
        h = get_int_handler(vector, cpu);
        h->handler = handler;
        h->arg = arg;
    }

    spin_unlock_restore(&gicd_lock, state, GICD_LOCK_FLAGS);
}

bool is_valid_interrupt(unsigned int vector, uint32_t flags)
{
    return (vector < MAX_INT);
}

static void gic_wait_for_rwp(uint32_t reg)
{
    int count = 1000000;
    while (GICREG(0, reg) & (1 << 31)) {
        count -= 1;
        if (!count) {
            LTRACEF("arm_gicv3: rwp timeout 0x%x\n", GICREG(0, reg));
            return;
        }
    }
}

static void gic_set_enable(uint vector, bool enable)
{
    int reg = vector / 32;
    uint32_t mask = 1ULL << (vector % 32);

    if (vector < 32) {
        for (int i = 0; i < SMP_MAX_CPUS; i++) {
            if (enable) {
                GICREG(0, GICR_ISENABLER0(i)) = mask;
            } else {
                GICREG(0, GICR_ICENABLER0(i)) = mask;
            }
            gic_wait_for_rwp(GICR_CTLR(i));
        }
    } else {
        if (enable) {
            GICREG(0, GICD_ISENABLER(reg)) = mask;
        } else {
            GICREG(0, GICD_ICENABLER(reg)) = mask;
        }
        gic_wait_for_rwp(GICD_CTLR);
    }
}

static void arm_gic_init_percpu(uint level)
{
    uint cpu = arch_curr_cpu_num();

    // configure sgi/ppi as non-secure group 1
    GICREG(0, GICR_IGROUPR0(cpu)) = ~0;
    gic_wait_for_rwp(GICR_CTLR(cpu));

    // clear and mask sgi/ppi
    GICREG(0, GICR_ICENABLER0(cpu)) = 0xffffffff;
    GICREG(0, GICR_ICPENDR0(cpu)) = ~0;
    gic_wait_for_rwp(GICR_CTLR(cpu));

    // TODO lpi init

    uint32_t sre = gic_read_sre();
    if (!(sre & 0x1)) {
        gic_write_sre(sre | 0x1);
        sre = gic_read_sre();
        assert(sre & 0x1);
    }

    // set priority threshold to max
    gic_write_pmr(0xff);

    // TODO EOI deactivates interrupt - revisit
    gic_write_ctlr(0);

    // enable group 1 interrupts
    gic_write_igrpen(1);
}

LK_INIT_HOOK_FLAGS(arm_gic_init_percpu, arm_gic_init_percpu, LK_INIT_LEVEL_PLATFORM_EARLY, LK_INIT_FLAG_SECONDARY_CPUS);

void arm_gic_init(void)
{
    __UNUSED uint rev = (GICREG(0, GICD_PIDR2) >> 4) & 0xf;
    assert(rev == 3 || rev == 4);

    uint32_t typer = GICREG(0, GICD_TYPER);
    __UNUSED uint idbits = (typer >> 19) & 0x1f;
    assert((idbits + 1) * 32 <= MAX_INT);

    // disable the distributor
    GICREG(0, GICD_CTLR) = 0;
    gic_wait_for_rwp(GICD_CTLR);
    ISB;

    // mask and clear spi
    int i;
    for (i = 32; i < MAX_INT; i += 32) {
        GICREG(0, GICD_ICENABLER(i / 32)) = ~0;
        GICREG(0, GICD_ICPENDR(i / 32)) = ~0;
    }
    gic_wait_for_rwp(GICD_CTLR);

    // enable distributor with ARE, group 1 enable
    GICREG(0, GICD_CTLR) = (1 << 4) | (1 << 1) | (1 << 0);
    gic_wait_for_rwp(GICD_CTLR);

    // set spi to target cpu 0. must do this after ARE enable
    uint max_cpu = (typer >> 5) & 0x7;
    if (max_cpu > 0) {
        for (i = 32; i < MAX_INT; i++) {
            GICREG64(0, GICD_IROUTER(i)) = 0;
        }
    }

    arm_gic_init_percpu(0);
}

status_t arm_gic_sgi(u_int irq, u_int flags, u_int cpu_mask)
{
    if (flags != ARM_GIC_SGI_FLAG_NS) {
        return ERR_INVALID_ARGS;
    }

    if (irq >= 16) {
        return ERR_INVALID_ARGS;
    }

    smp_wmb();

    uint cpu = 0;
    uint cluster = 0;
    uint64_t val = 0;
    while (cpu_mask && cpu < SMP_MAX_CPUS) {
        u_int mask = 0;
        while (arch_cpu_num_to_cluster_id(cpu) == cluster) {
            if (cpu_mask & (1u << cpu)) {
                mask |= 1u << arch_cpu_num_to_cpu_id(cpu);
                cpu_mask &= ~(1u << cpu);
            }
            cpu += 1;
        }

        val = ((irq & 0xf) << 24) |
              ((cluster & 0xff) << 16) |
              (mask & 0xff);

        gic_write_sgi1r(val);
        cluster += 1;
    }

    return NO_ERROR;
}

status_t mask_interrupt(unsigned int vector)
{
    if (vector >= MAX_INT)
        return ERR_INVALID_ARGS;

    if (arm_gic_interrupt_change_allowed(vector))
        gic_set_enable(vector, false);

    return NO_ERROR;
}

status_t unmask_interrupt(unsigned int vector)
{
    if (vector >= MAX_INT)
        return ERR_INVALID_ARGS;

    if (arm_gic_interrupt_change_allowed(vector))
        gic_set_enable(vector, true);

    return NO_ERROR;
}

status_t configure_interrupt(unsigned int vector,
                             enum interrupt_trigger_mode tm,
                             enum interrupt_polarity pol)
{
    if (vector <= 15 || vector >= MAX_INT) {
        return ERR_INVALID_ARGS;
    }

    if (pol != IRQ_POLARITY_ACTIVE_HIGH) {
        // TODO: polarity should actually be configure through a GPIO controller
        return ERR_NOT_SUPPORTED;
    }

    uint reg = vector / 16;
    uint mask = 0x2 << ((vector % 16) * 2);
    uint32_t val = GICREG(0, GICD_ICFGR(reg));
    if (tm == IRQ_TRIGGER_MODE_EDGE) {
        val |= mask;
    } else {
        val &= ~mask;
    }
    GICREG(0, GICD_ICFGR(reg)) = val;

    return NO_ERROR;
}

status_t get_interrupt_config(unsigned int vector,
                              enum interrupt_trigger_mode* tm,
                              enum interrupt_polarity* pol)
{
    if (vector >= MAX_INT)
        return ERR_INVALID_ARGS;

    if (tm)  *tm  = IRQ_TRIGGER_MODE_EDGE;
    if (pol) *pol = IRQ_POLARITY_ACTIVE_HIGH;

    return NO_ERROR;
}

unsigned int remap_interrupt(unsigned int vector) {
    return vector;
}

// called from assembly
enum handler_return platform_irq(struct iframe* frame) {
    // get the current vector
    uint32_t iar = gic_read_iar();
    unsigned vector = iar & 0x3ff;

    if (vector >= 0x3fe) {
        // spurious
        // TODO check this
        return INT_NO_RESCHEDULE;
    }

    // tracking external hardware irqs in this variable
    if (vector >= 32)
        THREAD_STATS_INC(interrupts);

    uint cpu = arch_curr_cpu_num();

    ktrace_tiny(TAG_IRQ_ENTER, (vector << 8) | cpu);

    LTRACEF_LEVEL(2, "iar 0x%x cpu %u currthread %p vector %u pc %#" PRIxPTR "\n", iar, cpu, get_current_thread(), vector, (uintptr_t)IFRAME_PC(frame));

    // deliver the interrupt
    enum handler_return ret = INT_NO_RESCHEDULE;
    struct int_handler_struct* handler = get_int_handler(vector, cpu);
    if (handler->handler) {
        ret = handler->handler(handler->arg);
    }

    gic_write_eoir(vector);

    LTRACEF_LEVEL(2, "cpu %u exit %u\n", cpu, ret);

    ktrace_tiny(TAG_IRQ_EXIT, (vector << 8) | cpu);

    return ret;
}

enum handler_return platform_fiq(struct iframe* frame) {
    PANIC_UNIMPLEMENTED;
}

status_t interrupt_send_ipi(mp_cpu_mask_t target, mp_ipi_t ipi) {
    uint gic_ipi_num = ipi + GIC_IPI_BASE;

    /* filter out targets outside of the range of cpus we care about */
    target &= ((1UL << SMP_MAX_CPUS) - 1);
    if (target != 0) {
        LTRACEF("target 0x%x, gic_ipi %u\n", target, gic_ipi_num);
        arm_gic_sgi(gic_ipi_num, ARM_GIC_SGI_FLAG_NS, target);
    }

    return NO_ERROR;
}

static enum handler_return arm_ipi_generic_handler(void *arg) {
    LTRACEF("cpu %u, arg %p\n", arch_curr_cpu_num(), arg);

    return mp_mbx_generic_irq();
}

static enum handler_return arm_ipi_reschedule_handler(void *arg) {
    LTRACEF("cpu %u, arg %p\n", arch_curr_cpu_num(), arg);

    return mp_mbx_reschedule_irq();
}

static enum handler_return arm_ipi_halt_handler(void *arg) {
    LTRACEF("cpu %u, arg %p\n", arch_curr_cpu_num(), arg);

    arch_disable_ints();
    for(;;);

    return INT_NO_RESCHEDULE;
}

void interrupt_init_percpu(void) {
    register_int_handler(MP_IPI_GENERIC + GIC_IPI_BASE, &arm_ipi_generic_handler, 0);
    register_int_handler(MP_IPI_RESCHEDULE + GIC_IPI_BASE, &arm_ipi_reschedule_handler, 0);
    register_int_handler(MP_IPI_HALT + GIC_IPI_BASE, &arm_ipi_halt_handler, 0);
    mp_set_curr_cpu_online(true);
    unmask_interrupt(MP_IPI_GENERIC + GIC_IPI_BASE);
    unmask_interrupt(MP_IPI_RESCHEDULE + GIC_IPI_BASE);
    unmask_interrupt(MP_IPI_HALT + GIC_IPI_BASE);
}
