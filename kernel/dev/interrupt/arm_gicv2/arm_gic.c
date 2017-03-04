// Copyright 2016 The Fuchsia Authors
// Copyright (c) 2012-2015 Travis Geiselbrecht
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <assert.h>
#include <bits.h>
#include <err.h>
#include <inttypes.h>
#include <sys/types.h>
#include <debug.h>
#include <dev/interrupt/arm_gic.h>
#include <dev/interrupt/arm_gic_regs.h>
#include <reg.h>
#include <kernel/thread.h>
#include <lk/init.h>
#include <dev/interrupt.h>
#include <arch/ops.h>
#include <trace.h>
#include <lib/ktrace.h>

#define LOCAL_TRACE 0

#include <arch/arm64.h>
#define iframe arm64_iframe_short
#define IFRAME_PC(frame) ((frame)->elr)

static spin_lock_t gicd_lock;
#define GICD_LOCK_FLAGS SPIN_LOCK_FLAG_INTERRUPTS
#define GIC_MAX_PER_CPU_INT 32

#ifndef GIC_IPI_BASE
#define GIC_IPI_BASE (16 - MAX_IPI)
#endif

static bool arm_gic_interrupt_change_allowed(int irq)
{
    return true;
}

static void suspend_resume_fiq(bool resume_gicc, bool resume_gicd)
{
}

struct int_handler_struct {
    int_handler handler;
    void *arg;
};

static struct int_handler_struct int_handler_table_per_cpu[GIC_MAX_PER_CPU_INT][SMP_MAX_CPUS];
static struct int_handler_struct int_handler_table_shared[MAX_INT-GIC_MAX_PER_CPU_INT];

static struct int_handler_struct *get_int_handler(unsigned int vector, uint cpu)
{
    if (vector < GIC_MAX_PER_CPU_INT)
        return &int_handler_table_per_cpu[vector][cpu];
    else
        return &int_handler_table_shared[vector - GIC_MAX_PER_CPU_INT];
}

void register_int_handler(unsigned int vector, int_handler handler, void *arg)
{
    struct int_handler_struct *h;
    uint cpu = arch_curr_cpu_num();

    spin_lock_saved_state_t state;

    if (vector >= MAX_INT)
        panic("register_int_handler: vector out of range %u\n", vector);

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

static DEFINE_GIC_SHADOW_REG(gicd_itargetsr, 4, 0x01010101, 32);

static void gic_set_enable(uint vector, bool enable)
{
    int reg = vector / 32;
    uint32_t mask = 1ULL << (vector % 32);

    if (enable)
        GICREG(0, GICD_ISENABLER(reg)) = mask;
    else
        GICREG(0, GICD_ICENABLER(reg)) = mask;
}

static void arm_gic_init_percpu(uint level)
{
#if ARM_GIC_V3
    gic_write_sre(1);       // enable system register access
    gic_write_ctlr(1);      // enable GIC0
    gic_write_pmr(0xFF);    // unmask interrupts at all priority levels
    gic_write_igrpen1(1);   // and finally enable interrupts
#else
    GICREG(0, GICC_CTLR) = 1; // enable GIC0
    GICREG(0, GICC_PMR) = 0xFF; // unmask interrupts at all priority levels
#endif
}

LK_INIT_HOOK_FLAGS(arm_gic_init_percpu,
                   arm_gic_init_percpu,
                   LK_INIT_LEVEL_PLATFORM_EARLY, LK_INIT_FLAG_SECONDARY_CPUS);

static void arm_gic_suspend_cpu(uint level)
{
    suspend_resume_fiq(false, false);
}

LK_INIT_HOOK_FLAGS(arm_gic_suspend_cpu, arm_gic_suspend_cpu,
                   LK_INIT_LEVEL_PLATFORM, LK_INIT_FLAG_CPU_SUSPEND);

static void arm_gic_resume_cpu(uint level)
{
    spin_lock_saved_state_t state;
    bool resume_gicd = false;

    spin_lock_save(&gicd_lock, &state, GICD_LOCK_FLAGS);
    if (!(GICREG(0, GICD_CTLR) & 1)) {
        dprintf(SPEW, "%s: distibutor is off, calling arm_gic_init instead\n", __func__);
        arm_gic_init();
        resume_gicd = true;
    } else {
        arm_gic_init_percpu(0);
    }
    spin_unlock_restore(&gicd_lock, state, GICD_LOCK_FLAGS);
    suspend_resume_fiq(true, resume_gicd);
}

LK_INIT_HOOK_FLAGS(arm_gic_resume_cpu, arm_gic_resume_cpu,
                   LK_INIT_LEVEL_PLATFORM, LK_INIT_FLAG_CPU_RESUME);

static int arm_gic_max_cpu(void)
{
    return (GICREG(0, GICD_TYPER) >> 5) & 0x7;
}

void arm_gic_init(void)
{
    int i;

    uint max_irqs = ((GICREG(0, GICD_TYPER) & 0x1F) + 1) * 32;
    printf("arm_gic_init max_irqs: %u\n", max_irqs);
    assert(max_irqs <= MAX_INT);

    for (i = 0; i < MAX_INT; i+= 32) {
        GICREG(0, GICD_ICENABLER(i / 32)) = ~0;
        GICREG(0, GICD_ICPENDR(i / 32)) = ~0;
    }

    if (arm_gic_max_cpu() > 0) {
        /* Set external interrupts to target cpu 0 */
        for (i = 32; i < MAX_INT; i += 4) {
            GICREG(0, GICD_ITARGETSR(i / 4)) = gicd_itargetsr[i / 4];
        }
    }

    GICREG(0, GICD_CTLR) = 1; // enable GIC0

    arm_gic_init_percpu(0);
}

status_t arm_gic_sgi(u_int irq, u_int flags, u_int cpu_mask)
{
    u_int val =
        ((flags & ARM_GIC_SGI_FLAG_TARGET_FILTER_MASK) << 24) |
        ((cpu_mask & 0xff) << 16) |
        ((flags & ARM_GIC_SGI_FLAG_NS) ? (1U << 15) : 0) |
        (irq & 0xf);

    if (irq >= 16)
        return ERR_INVALID_ARGS;

    LTRACEF("GICD_SGIR: %x\n", val);

    GICREG(0, GICD_SGIR) = val;

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
    if (vector >= MAX_INT)
        return ERR_INVALID_ARGS;

    if (tm != IRQ_TRIGGER_MODE_EDGE) {
        // We don't currently support non-edge triggered interupts via the GIC,
        // and we pre-initialize everything to edge triggered.
        // TODO: re-evaluate this.
        return ERR_NOT_SUPPORTED;
    }

    if (pol != IRQ_POLARITY_ACTIVE_HIGH) {
        // TODO: polarity should actually be configure through a GPIO controller
        return ERR_NOT_SUPPORTED;
    }

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

unsigned int remap_interrupt(unsigned int vector)
{
    return vector;
}

// called from assembly
enum handler_return platform_irq(struct iframe *frame)
{
    // get the current vector
#if ARM_GIC_V3
    uint32_t iar = gic_read_iar1();
#else
    uint32_t iar = GICREG(0, GICC_IAR);
#endif
    unsigned int vector = iar & 0x3ff;

    if (vector >= 0x3fe) {
        // spurious
        return INT_NO_RESCHEDULE;
    }

    // tracking external hardware irqs in this variable
    if (vector >= 32)
        THREAD_STATS_INC(interrupts);

    uint cpu = arch_curr_cpu_num();

    ktrace_tiny(TAG_IRQ_ENTER, (vector << 8) | cpu);

    LTRACEF_LEVEL(2, "iar 0x%x cpu %u currthread %p vector %u pc %#"
                  PRIxPTR "\n", iar, cpu,
                  get_current_thread(), vector, (uintptr_t)IFRAME_PC(frame));

    // deliver the interrupt
    enum handler_return ret;

    ret = INT_NO_RESCHEDULE;
    struct int_handler_struct *handler = get_int_handler(vector, cpu);
    if (handler->handler)
        ret = handler->handler(handler->arg);

#if ARM_GIC_V3
    gic_write_eoir1(vector);
#else
    GICREG(0, GICC_EOIR) = iar;
#endif

    LTRACEF_LEVEL(2, "cpu %u exit %u\n", cpu, ret);

    ktrace_tiny(TAG_IRQ_EXIT, (vector << 8) | cpu);

    return ret;
}

// called from assembly
enum handler_return platform_fiq(struct iframe *frame)
{
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
