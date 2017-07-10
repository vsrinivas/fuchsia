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
#include <kernel/thread.h>
#include <kernel/stats.h>
#include <kernel/vm.h>
#include <lk/init.h>
#include <dev/interrupt.h>
#include <trace.h>
#include <lib/ktrace.h>

#include <mdi/mdi.h>
#include <mdi/mdi-defs.h>
#include <pdev/driver.h>
#include <pdev/interrupt.h>

#define LOCAL_TRACE 0

#include <arch/arm64.h>
#define IFRAME_PC(frame) ((frame)->elr)

#include <arch/arch_ops.h>

// values read from MDI
static uint64_t arm_gicv3_gic_base = 0;
static uint64_t arm_gicv3_gicd_offset = 0;
static uint64_t arm_gicv3_gicr_offset = 0;
static uint64_t arm_gicv3_gicr_stride = 0;
static uint32_t ipi_base = 0;

// this header uses the arm_gicv3_gic_* variables above
#include <dev/interrupt/arm_gicv3_regs.h>

static uint gic_max_int;

static bool gic_is_valid_interrupt(unsigned int vector, uint32_t flags)
{
    return (vector < gic_max_int);
}

static void gic_wait_for_rwp(uint64_t reg)
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
        for (uint i = 0; i < arch_max_num_cpus(); i++) {
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

static void gic_init_percpu_early(void)
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

static void gic_init(void)
{
    __UNUSED uint rev = (GICREG(0, GICD_PIDR2) >> 4) & 0xf;
    assert(rev == 3 || rev == 4);

    uint32_t typer = GICREG(0, GICD_TYPER);
    __UNUSED uint idbits = (typer >> 19) & 0x1f;
    gic_max_int = (idbits + 1) * 32;
    printf("gic_init max_irqs: %u\n", gic_max_int);

    // disable the distributor
    GICREG(0, GICD_CTLR) = 0;
    gic_wait_for_rwp(GICD_CTLR);
    ISB;

    // mask and clear spi
    uint i;
    for (i = 32; i < gic_max_int; i += 32) {
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
        for (i = 32; i < gic_max_int; i++) {
            GICREG64(0, GICD_IROUTER(i)) = 0;
        }
    }

    gic_init_percpu_early();
}

static status_t arm_gic_sgi(u_int irq, u_int flags, u_int cpu_mask)
{
    if (flags != ARM_GIC_SGI_FLAG_NS) {
        return MX_ERR_INVALID_ARGS;
    }

    if (irq >= 16) {
        return MX_ERR_INVALID_ARGS;
    }

    smp_mb();

    uint cpu = 0;
    uint cluster = 0;
    uint64_t val = 0;
    while (cpu_mask && cpu < arch_max_num_cpus()) {
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

    return MX_OK;
}

static status_t gic_mask_interrupt(unsigned int vector)
{
    if (vector >= gic_max_int)
        return MX_ERR_INVALID_ARGS;

    gic_set_enable(vector, false);

    return MX_OK;
}

static status_t gic_unmask_interrupt(unsigned int vector)
{
    if (vector >= gic_max_int)
        return MX_ERR_INVALID_ARGS;

    gic_set_enable(vector, true);

    return MX_OK;
}

static status_t gic_configure_interrupt(unsigned int vector,
                             enum interrupt_trigger_mode tm,
                             enum interrupt_polarity pol)
{
    if (vector <= 15 || vector >= gic_max_int) {
        return MX_ERR_INVALID_ARGS;
    }

    if (pol != IRQ_POLARITY_ACTIVE_HIGH) {
        // TODO: polarity should actually be configure through a GPIO controller
        return MX_ERR_NOT_SUPPORTED;
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

    return MX_OK;
}

static status_t gic_get_interrupt_config(unsigned int vector,
                              enum interrupt_trigger_mode* tm,
                              enum interrupt_polarity* pol)
{
    if (vector >= gic_max_int)
        return MX_ERR_INVALID_ARGS;

    if (tm)  *tm  = IRQ_TRIGGER_MODE_EDGE;
    if (pol) *pol = IRQ_POLARITY_ACTIVE_HIGH;

    return MX_OK;
}

static unsigned int gic_remap_interrupt(unsigned int vector) {
    return vector;
}

// called from assembly
static enum handler_return gic_handle_irq(iframe* frame) {
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
        CPU_STATS_INC(interrupts);

    uint cpu = arch_curr_cpu_num();

    ktrace_tiny(TAG_IRQ_ENTER, (vector << 8) | cpu);

    LTRACEF_LEVEL(2, "iar 0x%x cpu %u currthread %p vector %u pc %#" PRIxPTR "\n", iar, cpu, get_current_thread(), vector, (uintptr_t)IFRAME_PC(frame));

    // deliver the interrupt
    enum handler_return ret = INT_NO_RESCHEDULE;
    struct int_handler_struct* handler = pdev_get_int_handler(vector);
    if (handler->handler) {
        ret = handler->handler(handler->arg);
    }

    gic_write_eoir(vector);

    LTRACEF_LEVEL(2, "cpu %u exit %u\n", cpu, ret);

    ktrace_tiny(TAG_IRQ_EXIT, (vector << 8) | cpu);

    return ret;
}

static enum handler_return gic_handle_fiq(iframe* frame) {
    PANIC_UNIMPLEMENTED;
}

static status_t gic_send_ipi(mp_cpu_mask_t target, mp_ipi_t ipi) {
    uint gic_ipi_num = ipi + ipi_base;

    /* filter out targets outside of the range of cpus we care about */
    target &= ((1UL << arch_max_num_cpus()) - 1);
    if (target != 0) {
        LTRACEF("target 0x%x, gic_ipi %u\n", target, gic_ipi_num);
        arm_gic_sgi(gic_ipi_num, ARM_GIC_SGI_FLAG_NS, target);
    }

    return MX_OK;
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

static void arm_gic_v3_init(mdi_node_ref_t* node, uint level) {
    uint64_t gic_base_virt = 0;

    bool got_gic_base_virt = false;
    bool got_gicd_offset = false;
    bool got_gicr_offset = false;
    bool got_gicr_stride = false;
    bool got_ipi_base = false;

    mdi_node_ref_t child;
    mdi_each_child(node, &child) {
        switch (mdi_id(&child)) {
        case MDI_BASE_VIRT:
            got_gic_base_virt = !mdi_node_uint64(&child, &gic_base_virt);
            break;
        case MDI_ARM_GIC_V3_GICD_OFFSET:
            got_gicd_offset = !mdi_node_uint64(&child, &arm_gicv3_gicd_offset);
            break;
        case MDI_ARM_GIC_V3_GICR_OFFSET:
            got_gicr_offset = !mdi_node_uint64(&child, &arm_gicv3_gicr_offset);
            break;
        case MDI_ARM_GIC_V3_GICR_STRIDE:
            got_gicr_stride = !mdi_node_uint64(&child, &arm_gicv3_gicr_stride);
            break;
        case MDI_ARM_GIC_V3_IPI_BASE:
            got_ipi_base = !mdi_node_uint32(&child, &ipi_base);
            break;
        }
    }


    if (!got_gic_base_virt) {
        printf("arm-gic-v3: gic_base_virt not defined\n");
        return;
    }
    if (!got_gicd_offset) {
        printf("arm-gic-v3: gicd_offset not defined\n");
        return;
    }
    if (!got_gicr_offset) {
        printf("arm-gic-v3: gicr_offset not defined\n");
        return;
    }
    if (!got_gicr_stride) {
        printf("arm-gic-v3: gicr_stride not defined\n");
        return;
    }
    if (!got_ipi_base) {
        printf("arm-gic-v3: ipi_base not defined\n");
        return;
    }

    arm_gicv3_gic_base = (uint64_t)gic_base_virt;

    gic_init();
    pdev_register_interrupts(&gic_ops);

    register_int_handler(MP_IPI_GENERIC + ipi_base, &arm_ipi_generic_handler, 0);
    register_int_handler(MP_IPI_RESCHEDULE + ipi_base, &arm_ipi_reschedule_handler, 0);
    register_int_handler(MP_IPI_HALT + ipi_base, &arm_ipi_halt_handler, 0);
}

LK_PDEV_INIT(arm_gic_v3_init, MDI_ARM_GIC_V3, arm_gic_v3_init, LK_INIT_LEVEL_PLATFORM_EARLY);
