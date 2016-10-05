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
#include <kernel/debug.h>
#include <lk/init.h>
#include <dev/interrupt.h>
#include <arch/ops.h>
#include <trace.h>
#if WITH_LIB_SM
#include <lib/sm.h>
#include <lib/sm/sm_err.h>
#endif

#include <lib/ktrace.h>

#define LOCAL_TRACE 0

#if ARCH_ARM
#include <arch/arm.h>
#define iframe arm_iframe
#define IFRAME_PC(frame) ((frame)->pc)
#endif
#if ARCH_ARM64
#include <arch/arm64.h>
#define iframe arm64_iframe_short
#define IFRAME_PC(frame) ((frame)->elr)
#endif

static status_t arm_gic_set_secure_locked(u_int irq, bool secure);

static spin_lock_t gicd_lock;
#if WITH_LIB_SM
#define GICD_LOCK_FLAGS SPIN_LOCK_FLAG_IRQ_FIQ
#else
#define GICD_LOCK_FLAGS SPIN_LOCK_FLAG_INTERRUPTS
#endif
#define GIC_MAX_PER_CPU_INT 32

#if WITH_LIB_SM
static bool arm_gic_non_secure_interrupts_frozen;

static bool arm_gic_interrupt_change_allowed(int irq)
{
    if (!arm_gic_non_secure_interrupts_frozen)
        return true;

    TRACEF("change to interrupt %d ignored after booting ns\n", irq);
    return false;
}

static void suspend_resume_fiq(bool resume_gicc, bool resume_gicd);
#else
static bool arm_gic_interrupt_change_allowed(int irq)
{
    return true;
}

static void suspend_resume_fiq(bool resume_gicc, bool resume_gicd)
{
}
#endif


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

#if WITH_LIB_SM
static DEFINE_GIC_SHADOW_REG(gicd_igroupr, 32, ~0U, 0);
#endif
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
#if WITH_LIB_SM
    GICREG(0, GICC_CTLR) = 0xb; // enable GIC0 and select fiq mode for secure
    GICREG(0, GICD_IGROUPR(0)) = ~0U; /* GICD_IGROUPR0 is banked */
#else
    GICREG(0, GICC_CTLR) = 1; // enable GIC0
#endif
    GICREG(0, GICC_PMR) = 0xFF; // unmask interrupts at all priority levels
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
#if WITH_LIB_SM
    GICREG(0, GICD_CTLR) = 3; // enable GIC0 ns interrupts
    /*
     * Iterate through all IRQs and set them to non-secure
     * mode. This will allow the non-secure side to handle
     * all the interrupts we don't explicitly claim.
     */
    for (i = 32; i < MAX_INT; i += 32) {
        u_int reg = i / 32;
        GICREG(0, GICD_IGROUPR(reg)) = gicd_igroupr[reg];
    }
#endif
    arm_gic_init_percpu(0);
}

static status_t arm_gic_set_secure_locked(u_int irq, bool secure)
{
#if WITH_LIB_SM
    int reg = irq / 32;
    uint32_t mask = 1ULL << (irq % 32);

    if (irq >= MAX_INT)
        return ERR_INVALID_ARGS;

    if (secure)
        GICREG(0, GICD_IGROUPR(reg)) = (gicd_igroupr[reg] &= ~mask);
    else
        GICREG(0, GICD_IGROUPR(reg)) = (gicd_igroupr[reg] |= mask);
    LTRACEF("irq %d, secure %d, GICD_IGROUP%d = %x\n",
            irq, secure, reg, GICREG(0, GICD_IGROUPR(reg)));
#endif
    return NO_ERROR;
}

static status_t arm_gic_set_target_locked(u_int irq, u_int cpu_mask, u_int enable_mask)
{
    u_int reg = irq / 4;
    u_int shift = 8 * (irq % 4);
    u_int old_val;
    u_int new_val;

    cpu_mask = (cpu_mask & 0xff) << shift;
    enable_mask = (enable_mask << shift) & cpu_mask;

    old_val = GICREG(0, GICD_ITARGETSR(reg));
    new_val = (gicd_itargetsr[reg] & ~cpu_mask) | enable_mask;
    GICREG(0, GICD_ITARGETSR(reg)) = gicd_itargetsr[reg] = new_val;
    LTRACEF("irq %u, GICD_ITARGETSR%u %x => %x (got %x)\n",
            irq, reg, old_val, new_val, GICREG(0, GICD_ITARGETSR(reg)));

    return NO_ERROR;
}

static status_t arm_gic_get_priority(u_int irq)
{
    u_int reg = irq / 4;
    u_int shift = 8 * (irq % 4);
    return (GICREG(0, GICD_IPRIORITYR(reg)) >> shift) & 0xff;
}

static status_t arm_gic_set_priority_locked(u_int irq, uint8_t priority)
{
    u_int reg = irq / 4;
    u_int shift = 8 * (irq % 4);
    u_int mask = 0xff << shift;
    uint32_t regval;

    regval = GICREG(0, GICD_IPRIORITYR(reg));
    LTRACEF("irq %u, old GICD_IPRIORITYR%u = %x\n", irq, reg, regval);
    regval = (regval & ~mask) | ((uint32_t)priority << shift);
    GICREG(0, GICD_IPRIORITYR(reg)) = regval;
    LTRACEF("irq %u, new GICD_IPRIORITYR%u = %x, req %x\n",
            irq, reg, GICREG(0, GICD_IPRIORITYR(reg)), regval);

    return 0;
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

static
enum handler_return __platform_irq(struct iframe *frame)
{
    // get the current vector
    uint32_t iar = GICREG(0, GICC_IAR);
    unsigned int vector = iar & 0x3ff;

    if (vector >= 0x3fe) {
        // spurious
        return INT_NO_RESCHEDULE;
    }

    THREAD_STATS_INC(interrupts);
    KEVLOG_IRQ_ENTER(vector);

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

    GICREG(0, GICC_EOIR) = iar;

    LTRACEF_LEVEL(2, "cpu %u exit %u\n", cpu, ret);

    ktrace_tiny(TAG_IRQ_EXIT, (vector << 8) | cpu);

    KEVLOG_IRQ_EXIT(vector);

    return ret;
}

// called from assembly
enum handler_return platform_irq(struct iframe *frame)
{
#if WITH_LIB_SM
    uint32_t ahppir = GICREG(0, GICC_AHPPIR);
    uint32_t pending_irq = ahppir & 0x3ff;
    struct int_handler_struct *h;
    uint cpu = arch_curr_cpu_num();

    LTRACEF("ahppir %d\n", ahppir);
    if (pending_irq < MAX_INT && get_int_handler(pending_irq, cpu)->handler) {
        enum handler_return ret = 0;
        uint32_t irq;
        uint8_t old_priority;
        spin_lock_saved_state_t state;

        spin_lock_save(&gicd_lock, &state, GICD_LOCK_FLAGS);

        /* Temporarily raise the priority of the interrupt we want to
         * handle so another interrupt does not take its place before
         * we can acknowledge it.
         */
        old_priority = arm_gic_get_priority(pending_irq);
        arm_gic_set_priority_locked(pending_irq, 0);
        DSB;
        irq = GICREG(0, GICC_AIAR) & 0x3ff;
        arm_gic_set_priority_locked(pending_irq, old_priority);

        spin_unlock_restore(&gicd_lock, state, GICD_LOCK_FLAGS);

        LTRACEF("irq %d\n", irq);
        if (irq < MAX_INT && (h = get_int_handler(pending_irq, cpu))->handler)
            ret = h->handler(h->arg);
        else
            TRACEF("unexpected irq %d != %d may get lost\n", irq, pending_irq);
        GICREG(0, GICC_AEOIR) = irq;
        return ret;
    }
    return sm_handle_irq();
#else
    return __platform_irq(frame);
#endif
}

// called from assembly
enum handler_return platform_fiq(struct iframe *frame)
{
#if WITH_LIB_SM
    sm_handle_fiq();
#else
    PANIC_UNIMPLEMENTED;
#endif
}

#if WITH_LIB_SM
static status_t arm_gic_get_next_irq_locked(u_int min_irq, bool per_cpu)
{
    u_int irq;
    u_int max_irq = per_cpu ? GIC_MAX_PER_CPU_INT : MAX_INT;
    uint cpu = arch_curr_cpu_num();

    if (!per_cpu && min_irq < GIC_MAX_PER_CPU_INT)
        min_irq = GIC_MAX_PER_CPU_INT;

    for (irq = min_irq; irq < max_irq; irq++)
        if (get_int_handler(irq, cpu)->handler)
            return irq;

    return SM_ERR_END_OF_INPUT;
}

long smc_intc_get_next_irq(smc32_args_t *args)
{
    status_t ret;
    spin_lock_saved_state_t state;

    spin_lock_save(&gicd_lock, &state, GICD_LOCK_FLAGS);

    arm_gic_non_secure_interrupts_frozen = true;
    ret = arm_gic_get_next_irq_locked(args->params[0], args->params[1]);
    LTRACEF("min_irq %d, per_cpu %d, ret %d\n",
            args->params[0], args->params[1], ret);

    spin_unlock_restore(&gicd_lock, state, GICD_LOCK_FLAGS);

    return ret;
}

static u_long enabled_fiq_mask[BITMAP_NUM_WORDS(MAX_INT)];

static void bitmap_update_locked(u_long *bitmap, u_int bit, bool set)
{
    u_long mask = 1UL << BITMAP_BIT_IN_WORD(bit);

    bitmap += BITMAP_WORD(bit);
    if (set)
        *bitmap |= mask;
    else
        *bitmap &= ~mask;
}

long smc_intc_request_fiq(smc32_args_t *args)
{
    u_int fiq = args->params[0];
    bool enable = args->params[1];
    spin_lock_saved_state_t state;

    dprintf(SPEW, "%s: fiq %d, enable %d\n", __func__, fiq, enable);
    spin_lock_save(&gicd_lock, &state, GICD_LOCK_FLAGS);

    arm_gic_set_secure_locked(fiq, true);
    arm_gic_set_target_locked(fiq, ~0, ~0);
    arm_gic_set_priority_locked(fiq, 0);

    gic_set_enable(fiq, enable);
    bitmap_update_locked(enabled_fiq_mask, fiq, enable);

    dprintf(SPEW, "%s: fiq %d, enable %d done\n", __func__, fiq, enable);

    spin_unlock_restore(&gicd_lock, state, GICD_LOCK_FLAGS);

    return NO_ERROR;
}

static u_int current_fiq[8] = { 0x3ff, 0x3ff, 0x3ff, 0x3ff, 0x3ff, 0x3ff, 0x3ff, 0x3ff };

static bool update_fiq_targets(u_int cpu, bool enable, u_int triggered_fiq, bool resume_gicd)
{
    u_int i, j;
    u_long mask;
    u_int fiq;
    bool smp = arm_gic_max_cpu() > 0;
    bool ret = false;

    spin_lock(&gicd_lock); /* IRQs and FIQs are already masked */
    for (i = 0; i < BITMAP_NUM_WORDS(MAX_INT); i++) {
        mask = enabled_fiq_mask[i];
        while (mask) {
            j = _ffz(~mask);
            mask &= ~(1UL << j);
            fiq = i * BITMAP_BITS_PER_WORD + j;
            if (fiq == triggered_fiq)
                ret = true;
            LTRACEF("cpu %d, irq %i, enable %d\n", cpu, fiq, enable);
            if (smp)
                arm_gic_set_target_locked(fiq, 1U << cpu, enable ? ~0 : 0);
            if (!smp || resume_gicd)
                gic_set_enable(fiq, enable);
        }
    }
    spin_unlock(&gicd_lock);
    return ret;
}

static void suspend_resume_fiq(bool resume_gicc, bool resume_gicd)
{
    u_int cpu = arch_curr_cpu_num();

    ASSERT(cpu < 8);

    update_fiq_targets(cpu, resume_gicc, ~0, resume_gicd);
}

status_t sm_intc_fiq_enter(void)
{
    u_int cpu = arch_curr_cpu_num();
    u_int irq = GICREG(0, GICC_IAR) & 0x3ff;
    bool fiq_enabled;

    ASSERT(cpu < 8);

    LTRACEF("cpu %d, irq %i\n", cpu, irq);

    if (irq >= 1020) {
        LTRACEF("spurious fiq: cpu %d, old %d, new %d\n", cpu, current_fiq[cpu], irq);
        return ERR_BAD_STATE;
    }

    fiq_enabled = update_fiq_targets(cpu, false, irq, false);
    GICREG(0, GICC_EOIR) = irq;

    if (current_fiq[cpu] != 0x3ff) {
        dprintf(INFO, "more than one fiq active: cpu %d, old %d, new %d\n", cpu, current_fiq[cpu], irq);
        return ERR_BAD_STATE;
    }

    if (!fiq_enabled) {
        dprintf(INFO, "got disabled fiq: cpu %d, new %d\n", cpu, irq);
        return ERR_UNAVAILABLE;
    }

    current_fiq[cpu] = irq;

    return 0;
}

void sm_intc_fiq_exit(void)
{
    u_int cpu = arch_curr_cpu_num();

    ASSERT(cpu < 8);

    LTRACEF("cpu %d, irq %i\n", cpu, current_fiq[cpu]);
    if (current_fiq[cpu] == 0x3ff) {
        dprintf(INFO, "%s: no fiq active, cpu %d\n", __func__, cpu);
        return;
    }
    update_fiq_targets(cpu, true, current_fiq[cpu], false);
    current_fiq[cpu] = 0x3ff;
}
#endif
