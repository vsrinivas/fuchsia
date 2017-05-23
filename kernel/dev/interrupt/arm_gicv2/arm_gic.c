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
#include <dev/interrupt/arm_gicv2m.h>
#include <reg.h>
#include <kernel/thread.h>
#include <lk/init.h>
#include <dev/interrupt.h>
#include <arch/ops.h>
#include <trace.h>
#include <lib/ktrace.h>

#include <mdi/mdi.h>
#include <mdi/mdi-defs.h>
#include <pdev/driver.h>
#include <pdev/interrupt.h>

#define LOCAL_TRACE 0

#include <arch/arm64.h>
#define iframe arm64_iframe_short
#define IFRAME_PC(frame) ((frame)->elr)

static spin_lock_t gicd_lock;
#define GICD_LOCK_FLAGS SPIN_LOCK_FLAG_INTERRUPTS

// values read from MDI
uint64_t arm_gicv2_gic_base = 0;
uint64_t arm_gicv2_gicd_offset = 0;
uint64_t arm_gicv2_gicc_offset = 0;
static uint32_t ipi_base = 0;

uint max_irqs = 0;

static paddr_t GICV2M_REG_FRAMES[] = { 0 };

static void arm_gic_init(void);

static void suspend_resume_fiq(bool resume_gicc, bool resume_gicd)
{
}

static bool gic_is_valid_interrupt(unsigned int vector, uint32_t flags)
{
    return (vector < max_irqs);
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

static void gic_init_percpu_early(void)
{
    GICREG(0, GICC_CTLR) = 1; // enable GIC0
    GICREG(0, GICC_PMR) = 0xFF; // unmask interrupts at all priority levels
}

static void arm_gic_suspend_cpu(uint level)
{
    suspend_resume_fiq(false, false);
}


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
        gic_init_percpu_early();
    }
    spin_unlock_restore(&gicd_lock, state, GICD_LOCK_FLAGS);
    suspend_resume_fiq(true, resume_gicd);
}

// disable for now. we will need to add suspend/resume support to dev/pdev for this to work
#if 0
LK_INIT_HOOK_FLAGS(arm_gic_suspend_cpu, arm_gic_suspend_cpu,
                   LK_INIT_LEVEL_PLATFORM, LK_INIT_FLAG_CPU_SUSPEND);

LK_INIT_HOOK_FLAGS(arm_gic_resume_cpu, arm_gic_resume_cpu,
                   LK_INIT_LEVEL_PLATFORM, LK_INIT_FLAG_CPU_RESUME);
#endif

static int arm_gic_max_cpu(void)
{
    return (GICREG(0, GICD_TYPER) >> 5) & 0x7;
}

static void arm_gic_init(void)
{
    uint i;

    max_irqs = ((GICREG(0, GICD_TYPER) & 0x1F) + 1) * 32;
    printf("arm_gic_init max_irqs: %u\n", max_irqs);
    assert(max_irqs <= MAX_INT);

    for (i = 0; i < max_irqs; i+= 32) {
        GICREG(0, GICD_ICENABLER(i / 32)) = ~0;
        GICREG(0, GICD_ICPENDR(i / 32)) = ~0;
    }

    if (arm_gic_max_cpu() > 0) {
        /* Set external interrupts to target cpu 0 */
        for (i = 32; i < max_irqs; i += 4) {
            GICREG(0, GICD_ITARGETSR(i / 4)) = gicd_itargetsr[i / 4];
        }
    }

    GICREG(0, GICD_CTLR) = 1; // enable GIC0

    gic_init_percpu_early();
}

static status_t arm_gic_sgi(u_int irq, u_int flags, u_int cpu_mask)
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

static status_t gic_mask_interrupt(unsigned int vector)
{
    if (vector >= max_irqs)
        return ERR_INVALID_ARGS;

    gic_set_enable(vector, false);

    return NO_ERROR;
}

static status_t gic_unmask_interrupt(unsigned int vector)
{
    if (vector >= max_irqs)
        return ERR_INVALID_ARGS;

    gic_set_enable(vector, true);

    return NO_ERROR;
}

static status_t gic_configure_interrupt(unsigned int vector,
                                        enum interrupt_trigger_mode tm,
                                        enum interrupt_polarity pol)
{
    if (vector >= max_irqs)
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

static status_t gic_get_interrupt_config(unsigned int vector,
                                         enum interrupt_trigger_mode* tm,
                                         enum interrupt_polarity* pol)
{
    if (vector >= max_irqs)
        return ERR_INVALID_ARGS;

    if (tm)  *tm  = IRQ_TRIGGER_MODE_EDGE;
    if (pol) *pol = IRQ_POLARITY_ACTIVE_HIGH;

    return NO_ERROR;
}

static unsigned int gic_remap_interrupt(unsigned int vector)
{
    return vector;
}

static enum handler_return gic_handle_irq(struct iframe *frame)
{
    // get the current vector
    uint32_t iar = GICREG(0, GICC_IAR);
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
    struct int_handler_struct* handler = pdev_get_int_handler(vector);
    if (handler->handler) {
        ret = handler->handler(handler->arg);
    }

    GICREG(0, GICC_EOIR) = iar;

    LTRACEF_LEVEL(2, "cpu %u exit %u\n", cpu, ret);

    ktrace_tiny(TAG_IRQ_EXIT, (vector << 8) | cpu);

    return ret;
}

static enum handler_return gic_handle_fiq(struct iframe *frame)
{
    PANIC_UNIMPLEMENTED;
}

static status_t gic_send_ipi(mp_cpu_mask_t target, mp_ipi_t ipi) {
    uint gic_ipi_num = ipi + ipi_base;

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

static void gic_init_percpu(void) {
    mp_set_curr_cpu_online(true);
    unmask_interrupt(MP_IPI_GENERIC + ipi_base);
    unmask_interrupt(MP_IPI_RESCHEDULE + ipi_base);
    unmask_interrupt(MP_IPI_HALT + ipi_base);
}

static void gic_shutdown(void) {
    PANIC_UNIMPLEMENTED;
}

static const struct pdev_interrupt_ops gic_ops = {
    .mask = gic_mask_interrupt,
    .unmask = gic_unmask_interrupt,
    .configure = gic_configure_interrupt,
    .get_config = gic_get_interrupt_config,
    .is_valid = gic_is_valid_interrupt,
    .remap = gic_remap_interrupt,
    .send_ipi = gic_send_ipi,
    .init_percpu_early = gic_init_percpu_early,
    .init_percpu = gic_init_percpu,
    .handle_irq = gic_handle_irq,
    .handle_fiq = gic_handle_fiq,
    .shutdown = gic_shutdown,
};

static void arm_gic_v2_init(mdi_node_ref_t* node, uint level) {
    uint64_t gic_base_virt = 0;
    uint64_t msi_frame_phys = 0;

    bool got_gic_base_virt = false;
    bool got_gicd_offset = false;
    bool got_gicc_offset = false;
    bool got_ipi_base = false;

    mdi_node_ref_t child;
    mdi_each_child(node, &child) {
        switch (mdi_id(&child)) {
        case MDI_KERNEL_DRIVERS_ARM_GIC_V2_BASE_VIRT:
            got_gic_base_virt = !mdi_node_uint64(&child, &gic_base_virt);
            break;
        case MDI_KERNEL_DRIVERS_ARM_GIC_V2_GICD_OFFSET:
            got_gicd_offset = !mdi_node_uint64(&child, &arm_gicv2_gicd_offset);
            break;
        case MDI_KERNEL_DRIVERS_ARM_GIC_V2_GICC_OFFSET:
            got_gicc_offset = !mdi_node_uint64(&child, &arm_gicv2_gicc_offset);
            break;
        case MDI_KERNEL_DRIVERS_ARM_GIC_V2_IPI_BASE:
            got_ipi_base = !mdi_node_uint32(&child, &ipi_base);
            break;
        case MDI_KERNEL_DRIVERS_ARM_GIC_V2_MSI_FRAME_PHYS:
            mdi_node_uint64(&child, &msi_frame_phys);
        }
    }

    if (!got_gic_base_virt) {
        printf("arm-gic-v2: gic_base_virt not defined\n");
        return;
    }
    if (!got_gicd_offset) {
        printf("arm-gic-v2: gicd_offset not defined\n");
        return;
    }
    if (!got_gicc_offset) {
        printf("arm-gic-v2: gicc_offset not defined\n");
        return;
    }
    if (!got_ipi_base) {
        printf("arm-gic-v2: ipi_base not defined\n");
        return;
    }

    arm_gicv2_gic_base = (uint64_t)(gic_base_virt);

    arm_gic_init();
    if (msi_frame_phys) {
        GICV2M_REG_FRAMES[0] = msi_frame_phys;
        arm_gicv2m_init(GICV2M_REG_FRAMES, countof(GICV2M_REG_FRAMES));
    }
    pdev_register_interrupts(&gic_ops);

    register_int_handler(MP_IPI_GENERIC + ipi_base, &arm_ipi_generic_handler, 0);
    register_int_handler(MP_IPI_RESCHEDULE + ipi_base, &arm_ipi_reschedule_handler, 0);
    register_int_handler(MP_IPI_HALT + ipi_base, &arm_ipi_halt_handler, 0);
}

LK_PDEV_INIT(arm_gic_v2_init, MDI_KERNEL_DRIVERS_ARM_GIC_V2, arm_gic_v2_init, LK_INIT_LEVEL_PLATFORM_EARLY);
