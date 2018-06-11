// Copyright 2016 The Fuchsia Authors
// Copyright (c) 2012-2015 Travis Geiselbrecht
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <arch/ops.h>
#include <arch/arm64/periphmap.h>
#include <assert.h>
#include <bits.h>
#include <debug.h>
#include <dev/interrupt.h>
#include <dev/interrupt/arm_gic_common.h>
#include <arch/arm64/hypervisor/gic/gicv2.h>
#include <dev/interrupt/arm_gicv2_regs.h>
#include <dev/interrupt/arm_gicv2m.h>
#include <err.h>
#include <inttypes.h>
#include <kernel/stats.h>
#include <kernel/thread.h>
#include <lib/ktrace.h>
#include <lk/init.h>
#include <reg.h>
#include <sys/types.h>
#include <trace.h>
#include <zircon/types.h>

#include <pdev/driver.h>
#include <pdev/interrupt.h>
#include <zircon/boot/driver-config.h>

#define LOCAL_TRACE 0

#include <arch/arm64.h>
#define iframe arm64_iframe_short
#define IFRAME_PC(frame) ((frame)->elr)

static spin_lock_t gicd_lock;
#define GICD_LOCK_FLAGS SPIN_LOCK_FLAG_INTERRUPTS

// values read from zbi
vaddr_t arm_gicv2_gic_base = 0;
uint64_t arm_gicv2_gicd_offset = 0;
uint64_t arm_gicv2_gicc_offset = 0;
uint64_t arm_gicv2_gich_offset = 0;
uint64_t arm_gicv2_gicv_offset = 0;
static uint32_t ipi_base = 0;

uint max_irqs = 0;

static zx_status_t arm_gic_init(void);

static zx_status_t gic_configure_interrupt(unsigned int vector,
                                           enum interrupt_trigger_mode tm,
                                           enum interrupt_polarity pol);

static void suspend_resume_fiq(bool resume_gicc, bool resume_gicd) {
}

static bool gic_is_valid_interrupt(unsigned int vector, uint32_t flags) {
    return (vector < max_irqs);
}

static uint32_t gic_get_base_vector(void) {
    // ARM Generic Interrupt Controller v2 chapter 2.1
    // INTIDs 0-15 are local CPU interrupts
    return 16;
}

static uint32_t gic_get_max_vector(void) {
    return max_irqs;
}

static DEFINE_GIC_SHADOW_REG(gicd_itargetsr, 4, 0x01010101, 32);

static void gic_set_enable(uint vector, bool enable) {
    int reg = vector / 32;
    uint32_t mask = (uint32_t)(1ULL << (vector % 32));

    if (enable)
        GICREG(0, GICD_ISENABLER(reg)) = mask;
    else
        GICREG(0, GICD_ICENABLER(reg)) = mask;
}

static void gic_init_percpu_early(void) {
    GICREG(0, GICC_CTLR) = 1;   // enable GIC0
    GICREG(0, GICC_PMR) = 0xFF; // unmask interrupts at all priority levels
}

static void arm_gic_suspend_cpu(uint level) {
    suspend_resume_fiq(false, false);
}

static void arm_gic_resume_cpu(uint level) {
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

static int arm_gic_max_cpu(void) {
    return (GICREG(0, GICD_TYPER) >> 5) & 0x7;
}

static zx_status_t arm_gic_init(void) {
    uint i;
    // see if we're gic v2
    uint rev = 0;
    uint32_t pidr2 = GICREG(0, GICD_PIDR2);
    if (pidr2 != 0) {
        uint rev = BITS_SHIFT(pidr2, 7, 4);
        if (rev != GICV2) {
            return ZX_ERR_NOT_FOUND;
        }
    } else {
        // some v2's return a null PIDR2
        pidr2 = GICREG(0, GICD_V3_PIDR2);
        rev = BITS_SHIFT(pidr2, 7, 4);
        if (rev >= GICV3) {
            // looks like a gic v3
            return ZX_ERR_NOT_FOUND;
        }
        // HACK: if gicv2 and v3 pidr2 seems to be blank, assume we're v2 and continue
    }

    uint32_t typer = GICREG(0, GICD_TYPER);
    uint32_t it_lines_number = BITS_SHIFT(typer, 4, 0);
    max_irqs = (it_lines_number + 1) * 32;
    LTRACEF("arm_gic_init max_irqs: %u\n", max_irqs);
    assert(max_irqs <= MAX_INT);

    for (i = 0; i < max_irqs; i += 32) {
        GICREG(0, GICD_ICENABLER(i / 32)) = ~0;
        GICREG(0, GICD_ICPENDR(i / 32)) = ~0;
    }

    if (arm_gic_max_cpu() > 0) {
        /* Set external interrupts to target cpu 0 */
        for (i = 32; i < max_irqs; i += 4) {
            GICREG(0, GICD_ITARGETSR(i / 4)) = gicd_itargetsr[i / 4];
        }
    }
    // Initialize all the SPIs to edge triggered
    for (i = GIC_BASE_SPI; i < max_irqs; i++)
        gic_configure_interrupt(i, IRQ_TRIGGER_MODE_EDGE, IRQ_POLARITY_ACTIVE_HIGH);

    GICREG(0, GICD_CTLR) = 1; // enable GIC0

    gic_init_percpu_early();

    return ZX_OK;
}

static zx_status_t arm_gic_sgi(u_int irq, u_int flags, u_int cpu_mask) {
    u_int val =
        ((flags & ARM_GIC_SGI_FLAG_TARGET_FILTER_MASK) << 24) |
        ((cpu_mask & 0xff) << 16) |
        ((flags & ARM_GIC_SGI_FLAG_NS) ? (1U << 15) : 0) |
        (irq & 0xf);

    if (irq >= 16)
        return ZX_ERR_INVALID_ARGS;

    LTRACEF("GICD_SGIR: %x\n", val);

    GICREG(0, GICD_SGIR) = val;

    return ZX_OK;
}

static zx_status_t gic_mask_interrupt(unsigned int vector) {
    if (vector >= max_irqs)
        return ZX_ERR_INVALID_ARGS;

    gic_set_enable(vector, false);

    return ZX_OK;
}

static zx_status_t gic_unmask_interrupt(unsigned int vector) {
    if (vector >= max_irqs)
        return ZX_ERR_INVALID_ARGS;

    gic_set_enable(vector, true);

    return ZX_OK;
}

static zx_status_t gic_configure_interrupt(unsigned int vector,
                                           enum interrupt_trigger_mode tm,
                                           enum interrupt_polarity pol) {
    //Only configurable for SPI interrupts
    if ((vector >= max_irqs) || (vector < GIC_BASE_SPI))
        return ZX_ERR_INVALID_ARGS;

    if (pol != IRQ_POLARITY_ACTIVE_HIGH) {
        // TODO: polarity should actually be configure through a GPIO controller
        return ZX_ERR_NOT_SUPPORTED;
    }

    // type is encoded with two bits, MSB of the two determine type
    // 16 irqs encoded per ICFGR register
    uint32_t reg_ndx = vector >> 4;
    uint32_t bit_shift = ((vector & 0xf) << 1) + 1;
    uint32_t reg_val   = GICREG(0, GICD_ICFGR(reg_ndx));
    if (tm == IRQ_TRIGGER_MODE_EDGE) {
        reg_val |= (1 << bit_shift);
    } else {
        reg_val &= ~(1 << bit_shift);
    }
    GICREG(0, GICD_ICFGR(reg_ndx)) = reg_val;

    return ZX_OK;
}

static zx_status_t gic_get_interrupt_config(unsigned int vector,
                                            enum interrupt_trigger_mode* tm,
                                            enum interrupt_polarity* pol) {
    if (vector >= max_irqs)
        return ZX_ERR_INVALID_ARGS;

    if (tm)
        *tm = IRQ_TRIGGER_MODE_EDGE;
    if (pol)
        *pol = IRQ_POLARITY_ACTIVE_HIGH;

    return ZX_OK;
}

static unsigned int gic_remap_interrupt(unsigned int vector) {
    return vector;
}

static void gic_handle_irq(struct iframe* frame) {
    // get the current vector
    uint32_t iar = GICREG(0, GICC_IAR);
    unsigned int vector = iar & 0x3ff;

    if (vector >= 0x3fe) {
        // spurious
        return;
    }

    // tracking external hardware irqs in this variable
    if (vector >= 32)
        CPU_STATS_INC(interrupts);

    uint cpu = arch_curr_cpu_num();

    ktrace_tiny(TAG_IRQ_ENTER, (vector << 8) | cpu);

    LTRACEF_LEVEL(2, "iar 0x%x cpu %u currthread %p vector %u pc %#" PRIxPTR "\n", iar, cpu,
                  get_current_thread(), vector, (uintptr_t)IFRAME_PC(frame));

    // deliver the interrupt
    struct int_handler_struct* handler = pdev_get_int_handler(vector);
    if (handler->handler) {
        handler->handler(handler->arg);
    }

    GICREG(0, GICC_EOIR) = iar;

    LTRACEF_LEVEL(2, "cpu %u exit\n", cpu);

    ktrace_tiny(TAG_IRQ_EXIT, (vector << 8) | cpu);
}

static void gic_handle_fiq(struct iframe* frame) {
    PANIC_UNIMPLEMENTED;
}

static zx_status_t gic_send_ipi(cpu_mask_t target, mp_ipi_t ipi) {
    uint gic_ipi_num = ipi + ipi_base;

    /* filter out targets outside of the range of cpus we care about */
    target &= ((1UL << SMP_MAX_CPUS) - 1);
    if (target != 0) {
        LTRACEF("target 0x%x, gic_ipi %u\n", target, gic_ipi_num);
        arm_gic_sgi(gic_ipi_num, ARM_GIC_SGI_FLAG_NS, target);
    }

    return ZX_OK;
}

static void arm_ipi_halt_handler(void* arg) {
    LTRACEF("cpu %u, arg %p\n", arch_curr_cpu_num(), arg);

    arch_disable_ints();
    while(true) {}
}

static void gic_init_percpu(void) {
    mp_set_curr_cpu_online(true);
    unmask_interrupt(MP_IPI_GENERIC + ipi_base);
    unmask_interrupt(MP_IPI_RESCHEDULE + ipi_base);
    unmask_interrupt(MP_IPI_INTERRUPT + ipi_base);
    unmask_interrupt(MP_IPI_HALT + ipi_base);
}

static void gic_shutdown(void) {
    // Turn off all GIC0 interrupts at the distributor.
    GICREG(0, GICD_CTLR) = 0;
}

static const struct pdev_interrupt_ops gic_ops = {
    .mask = gic_mask_interrupt,
    .unmask = gic_unmask_interrupt,
    .configure = gic_configure_interrupt,
    .get_config = gic_get_interrupt_config,
    .is_valid = gic_is_valid_interrupt,
    .get_base_vector = gic_get_base_vector,
    .get_max_vector = gic_get_max_vector,
    .remap = gic_remap_interrupt,
    .send_ipi = gic_send_ipi,
    .init_percpu_early = gic_init_percpu_early,
    .init_percpu = gic_init_percpu,
    .handle_irq = gic_handle_irq,
    .handle_fiq = gic_handle_fiq,
    .shutdown = gic_shutdown,
};

static void arm_gic_v2_init(const void* driver_data, uint32_t length) {
    ASSERT(length >= sizeof(dcfg_arm_gicv2_driver_t));
    const dcfg_arm_gicv2_driver_t* driver = driver_data;
    ASSERT(driver->mmio_phys);

    arm_gicv2_gic_base = periph_paddr_to_vaddr(driver->mmio_phys);
    ASSERT(arm_gicv2_gic_base);
    arm_gicv2_gicd_offset = driver->gicd_offset;
    arm_gicv2_gicc_offset = driver->gicc_offset;
    arm_gicv2_gich_offset = driver->gich_offset;
    arm_gicv2_gicv_offset = driver->gicv_offset;
    ipi_base = driver->ipi_base;

    if (arm_gic_init() != ZX_OK) {
        if (driver->optional) {
            // failed to detect gic v2 but it's marked optional. continue
            return;
        }
        printf("GICv2: failed to detect GICv2, interrupts will be broken\n");
        return;
    }

    dprintf(SPEW, "detected GICv2\n");

    // pass the list of physical and virtual addresses for the GICv2m register apertures
    if (driver->msi_frame_phys) {
        // the following arrays must be static because arm_gicv2m_init stashes the pointer
        static paddr_t GICV2M_REG_FRAMES[] = {0};
        static vaddr_t GICV2M_REG_FRAMES_VIRT[] = {0};

        GICV2M_REG_FRAMES[0] = driver->msi_frame_phys;
        GICV2M_REG_FRAMES_VIRT[0] = periph_paddr_to_vaddr(driver->msi_frame_phys);
        ASSERT(GICV2M_REG_FRAMES_VIRT[0]);
        arm_gicv2m_init(GICV2M_REG_FRAMES, GICV2M_REG_FRAMES_VIRT, countof(GICV2M_REG_FRAMES));
    }
    pdev_register_interrupts(&gic_ops);

    zx_status_t status =
        gic_register_sgi_handler(MP_IPI_GENERIC + ipi_base, (void*)&mp_mbx_generic_irq, 0);
    DEBUG_ASSERT(status == ZX_OK);
    status =
        gic_register_sgi_handler(MP_IPI_RESCHEDULE + ipi_base, (void*)&mp_mbx_reschedule_irq, 0);
    DEBUG_ASSERT(status == ZX_OK);
    status = gic_register_sgi_handler(MP_IPI_INTERRUPT + ipi_base, (void*)&mp_mbx_interrupt_irq, 0);
    DEBUG_ASSERT(status == ZX_OK);
    status = gic_register_sgi_handler(MP_IPI_HALT + ipi_base, &arm_ipi_halt_handler, 0);
    DEBUG_ASSERT(status == ZX_OK);

    gicv2_hw_interface_register();
}

LK_PDEV_INIT(arm_gic_v2_init, KDRV_ARM_GIC_V2, arm_gic_v2_init, LK_INIT_LEVEL_PLATFORM_EARLY);
