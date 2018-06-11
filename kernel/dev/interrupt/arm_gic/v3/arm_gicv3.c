// Copyright 2017 The Fuchsia Authors
// Copyright (c) 2017, Google Inc. All rights reserved.
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <arch/arm64/periphmap.h>
#include <assert.h>
#include <bits.h>
#include <dev/interrupt.h>
#include <dev/interrupt/arm_gic_common.h>
#include <arch/arm64/hypervisor/gic/gicv3.h>
#include <err.h>
#include <inttypes.h>
#include <kernel/stats.h>
#include <kernel/thread.h>
#include <lib/ktrace.h>
#include <lk/init.h>
#include <string.h>
#include <trace.h>
#include <vm/vm.h>
#include <zircon/types.h>

#include <pdev/driver.h>
#include <pdev/interrupt.h>
#include <zircon/boot/driver-config.h>

#define LOCAL_TRACE 0

#include <arch/arm64.h>
#define IFRAME_PC(frame) ((frame)->elr)

#include <arch/arch_ops.h>

// values read from zbi
static vaddr_t arm_gicv3_gic_base = 0;
static uint64_t arm_gicv3_gicd_offset = 0;
static uint64_t arm_gicv3_gicr_offset = 0;
static uint64_t arm_gicv3_gicr_stride = 0;

/*
IMX8M Errata: e11171: CA53: Cannot support single-core runtime wakeup

According to the GIC500 specification and the Arm Trusted Firmware design, when a CPU
core enters the deepest CPU idle state (power-down), it must disable the GIC500 CPU
interface and set the Redistributor register to indicate that this CPU is in sleep state.

On NXP IMX8M, However, if the CPU core is in WFI or power-down with CPU interface disabled,
another core cannot wake-up the powered-down core using SGI interrupt.

One workaround is to use another A53 core for the IRQ0 which is controlled by the IOMUX
GPR to generate an external interrupt to wake-up the powered-down core.
The SW workaround is implemented into default BSP release. The workaround commit tag is
“MLK-16804-04 driver: irqchip: Add IPI SW workaround for imx8mq" on the linux-imx project
*/
static uint64_t mx8_gpr_virt = 0;

static uint32_t ipi_base = 0;

// this header uses the arm_gicv3_gic_* variables above
#include <dev/interrupt/arm_gicv3_regs.h>

static uint gic_max_int;

static bool gic_is_valid_interrupt(unsigned int vector, uint32_t flags) {
    return (vector < gic_max_int);
}

static uint32_t gic_get_base_vector(void) {
    // ARM Generic Interrupt Controller v3&4 chapter 2.2
    // INTIDs 0-15 are local CPU interrupts
    return 16;
}

static uint32_t gic_get_max_vector(void) {
    return gic_max_int;
}

static void gic_wait_for_rwp(uint64_t reg) {
    int count = 1000000;
    while (GICREG(0, reg) & (1 << 31)) {
        count -= 1;
        if (!count) {
            LTRACEF("arm_gicv3: rwp timeout 0x%x\n", GICREG(0, reg));
            return;
        }
    }
}

static void gic_set_enable(uint vector, bool enable) {
    int reg = vector / 32;
    uint32_t mask = (uint32_t)(1ULL << (vector % 32));

    if (vector < 32) {
        for (uint i = 0; i < arch_max_num_cpus(); i++) {
            if (enable) {
                GICREG(0, GICR_ISENABLER0(i)) = mask;
            }
            else {
                GICREG(0, GICR_ICENABLER0(i)) = mask;
            }
            gic_wait_for_rwp(GICR_CTLR(i));
        }
    } else {
        if (enable) {
            GICREG(0, GICD_ISENABLER(reg)) = mask;
        }
        else {
            GICREG(0, GICD_ICENABLER(reg)) = mask;
        }
        gic_wait_for_rwp(GICD_CTLR);
    }
}

static void gic_init_percpu_early(void) {
    uint cpu = arch_curr_cpu_num();

    // redistributer config: configure sgi/ppi as non-secure group 1.
    GICREG(0, GICR_IGROUPR0(cpu)) = ~0;
    gic_wait_for_rwp(GICR_CTLR(cpu));

    // redistributer config: clear and mask sgi/ppi.
    GICREG(0, GICR_ICENABLER0(cpu)) = 0xffffffff;
    GICREG(0, GICR_ICPENDR0(cpu)) = ~0;
    gic_wait_for_rwp(GICR_CTLR(cpu));

    // TODO lpi init

    // enable system register interface
    uint32_t sre = gic_read_sre();
    if (!(sre & 0x1)) {
        gic_write_sre(sre | 0x1);
        sre = gic_read_sre();
        assert(sre & 0x1);
    }

    // set priority threshold to max.
    gic_write_pmr(0xff);

    // TODO EOI deactivates interrupt - revisit.
    gic_write_ctlr(0);

    // enable group 1 interrupts.
    gic_write_igrpen(1);
}

static zx_status_t gic_init(void) {
    LTRACE_ENTRY;

    uint pidr2 = GICREG(0, GICD_PIDR2);
    uint rev = BITS_SHIFT(pidr2, 7, 4);
    if (rev != GICV3 && rev != GICV4)
        return ZX_ERR_NOT_FOUND;

    uint32_t typer = GICREG(0, GICD_TYPER);
    uint32_t idbits = BITS_SHIFT(typer, 23, 19);
    gic_max_int = (idbits + 1) * 32;

    // disable the distributor
    GICREG(0, GICD_CTLR) = 0;
    gic_wait_for_rwp(GICD_CTLR);
    ISB;

    // diistributer config: mask and clear all spis, set group 1.
    uint i;
    for (i = 32; i < gic_max_int; i += 32) {
        GICREG(0, GICD_ICENABLER(i / 32)) = ~0;
        GICREG(0, GICD_ICPENDR(i / 32)) = ~0;
        GICREG(0, GICD_IGROUPR(i / 32)) = ~0;
        GICREG(0, GICD_IGRPMODR(i / 32)) = 0;
    }
    gic_wait_for_rwp(GICD_CTLR);

    // enable distributor with ARE, group 1 enable
    GICREG(0, GICD_CTLR) = CTLR_ENALBE_G0 | CTLR_ENABLE_G1NS | CTLR_ARE_S;
    gic_wait_for_rwp(GICD_CTLR);

    // set spi to target cpu 0. must do this after ARE enable
    uint max_cpu = BITS_SHIFT(typer, 7, 5);
    if (max_cpu > 0) {
        for (i = 32; i < gic_max_int; i++) {
            GICREG64(0, GICD_IROUTER(i)) = 0;
        }
    }

    gic_init_percpu_early();

    mb();
    ISB;

    return ZX_OK;
}

static zx_status_t arm_gic_sgi(u_int irq, u_int flags, u_int cpu_mask) {
    if (flags != ARM_GIC_SGI_FLAG_NS) {
        return ZX_ERR_INVALID_ARGS;
    }

    if (irq >= 16) {
        return ZX_ERR_INVALID_ARGS;
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
        // Work around
        if (mx8_gpr_virt) {
            uint32_t regVal;
            // peinding irq32 to wakeup core
            regVal = *(volatile uint32_t *)(mx8_gpr_virt + 0x4);
            regVal |= (1 << 12);
            *(volatile uint32_t *)(mx8_gpr_virt + 0x4) = regVal;
            // delay
            spin(50);
            regVal &= ~(1 << 12);
            *(volatile uint32_t *)(mx8_gpr_virt + 0x4) = regVal;
        }
    }

    return ZX_OK;
}

static zx_status_t gic_mask_interrupt(unsigned int vector) {
    LTRACEF("vector %u\n", vector);

    if (vector >= gic_max_int)
        return ZX_ERR_INVALID_ARGS;

    gic_set_enable(vector, false);

    return ZX_OK;
}

static zx_status_t gic_unmask_interrupt(unsigned int vector) {
    LTRACEF("vector %u\n", vector);

    if (vector >= gic_max_int)
        return ZX_ERR_INVALID_ARGS;

    gic_set_enable(vector, true);

    return ZX_OK;
}

static zx_status_t gic_configure_interrupt(unsigned int vector,
                                           enum interrupt_trigger_mode tm,
                                           enum interrupt_polarity pol) {
    LTRACEF("vector %u, trigger mode %u, polarity %u\n", vector, tm, pol);

    if (vector <= 15 || vector >= gic_max_int) {
        return ZX_ERR_INVALID_ARGS;
    }

    if (pol != IRQ_POLARITY_ACTIVE_HIGH) {
        // TODO: polarity should actually be configure through a GPIO controller
        return ZX_ERR_NOT_SUPPORTED;
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

    return ZX_OK;
}

static zx_status_t gic_get_interrupt_config(unsigned int vector,
                                            enum interrupt_trigger_mode* tm,
                                            enum interrupt_polarity* pol) {
    LTRACEF("vector %u\n", vector);

    if (vector >= gic_max_int)
        return ZX_ERR_INVALID_ARGS;

    if (tm)
        *tm = IRQ_TRIGGER_MODE_EDGE;
    if (pol)
        *pol = IRQ_POLARITY_ACTIVE_HIGH;

    return ZX_OK;
}

static unsigned int gic_remap_interrupt(unsigned int vector) {
    LTRACEF("vector %u\n", vector);
    return vector;
}

// called from assembly
static void gic_handle_irq(iframe* frame) {
    // get the current vector
    uint32_t iar = gic_read_iar();
    unsigned vector = iar & 0x3ff;

    LTRACEF_LEVEL(2, "iar %#x, vector %u\n", iar, vector);

    if (vector >= 0x3fe) {
        // spurious
        // TODO check this
        return;
    }

    // tracking external hardware irqs in this variable
    if (vector >= 32)
        CPU_STATS_INC(interrupts);

    uint cpu = arch_curr_cpu_num();

    ktrace_tiny(TAG_IRQ_ENTER, (vector << 8) | cpu);

    LTRACEF_LEVEL(2, "iar 0x%x cpu %u currthread %p vector %u pc %#" PRIxPTR "\n",
                  iar, cpu, get_current_thread(), vector, (uintptr_t)IFRAME_PC(frame));

    // deliver the interrupt
    struct int_handler_struct* handler = pdev_get_int_handler(vector);
    if (handler->handler) {
        handler->handler(handler->arg);
    }

    gic_write_eoir(vector);

    LTRACEF_LEVEL(2, "cpu %u exit\n", cpu);

    ktrace_tiny(TAG_IRQ_EXIT, (vector << 8) | cpu);
}

static void gic_handle_fiq(iframe* frame) {
    PANIC_UNIMPLEMENTED;
}

static zx_status_t gic_send_ipi(cpu_mask_t target, mp_ipi_t ipi) {
    uint gic_ipi_num = ipi + ipi_base;

    /* filter out targets outside of the range of cpus we care about */
    target &= (cpu_mask_t)(((1UL << arch_max_num_cpus()) - 1));
    if (target != 0) {
        LTRACEF("target 0x%x, gic_ipi %u\n", target, gic_ipi_num);
        arm_gic_sgi(gic_ipi_num, ARM_GIC_SGI_FLAG_NS, target);
    }

    return ZX_OK;
}

static void arm_ipi_halt_handler(void* arg) {
    LTRACEF("cpu %u, arg %p\n", arch_curr_cpu_num(), arg);

    arch_disable_ints();
    for (;;) {};
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

static void arm_gic_v3_init(const void* driver_data, uint32_t length) {
    ASSERT(length >= sizeof(dcfg_arm_gicv3_driver_t));
    const dcfg_arm_gicv3_driver_t* driver = driver_data;
    ASSERT(driver->mmio_phys);

    LTRACE_ENTRY;

    // If a GIC driver is already registered to the GIC interface it's means we are running GICv2
    // and we do not need to initialize GICv3. Since we have added both GICv3 and GICv2 in board.mdi,
    // both drivers are initialized
    if(gicv3_is_gic_registered()) {
        return;
    }

    if (driver->mx8_gpr_phys) {
        printf("arm-gic-v3: Applying Errata e11171 for NXP MX8!\n");
        mx8_gpr_virt = periph_paddr_to_vaddr(driver->mx8_gpr_phys);
        ASSERT(mx8_gpr_virt);
    }

    arm_gicv3_gic_base = periph_paddr_to_vaddr(driver->mmio_phys);
    ASSERT(arm_gicv3_gic_base);
    arm_gicv3_gicd_offset = driver->gicd_offset;
    arm_gicv3_gicr_offset = driver->gicr_offset;
    arm_gicv3_gicr_stride = driver->gicr_stride;
    ipi_base = driver->ipi_base;

    arm_gicv3_gic_base = periph_paddr_to_vaddr(driver->mmio_phys);
    ASSERT(arm_gicv3_gic_base);

    if (gic_init() != ZX_OK) {
        if (driver->optional) {
            // failed to detect gic v3 but it's marked optional. continue
            return;
        }
        printf("GICv3: failed to detect GICv3, interrupts will be broken\n");
        return;
    }

    dprintf(SPEW, "detected GICv3\n");

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

    gicv3_hw_interface_register();

    LTRACE_EXIT;
}

LK_PDEV_INIT(arm_gic_v3_init, KDRV_ARM_GIC_V3, arm_gic_v3_init, LK_INIT_LEVEL_PLATFORM_EARLY);
