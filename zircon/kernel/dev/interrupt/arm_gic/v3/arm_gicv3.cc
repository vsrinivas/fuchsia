// Copyright 2017 The Fuchsia Authors
// Copyright (c) 2017, Google Inc. All rights reserved.
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <assert.h>
#include <bits.h>
#include <err.h>
#include <inttypes.h>
#include <lib/arch/intrin.h>
#include <lib/ktrace.h>
#include <lib/root_resource_filter.h>
#include <string.h>
#include <trace.h>
#include <zircon/boot/driver-config.h>
#include <zircon/types.h>

#include <arch/arm64/hypervisor/gic/gicv3.h>
#include <arch/arm64/periphmap.h>
#include <dev/interrupt.h>
#include <dev/interrupt/arm_gic_common.h>
#include <dev/interrupt/arm_gic_hw_interface.h>
#include <kernel/cpu.h>
#include <kernel/stats.h>
#include <kernel/thread.h>
#include <lk/init.h>
#include <pdev/driver.h>
#include <pdev/interrupt.h>
#include <vm/vm.h>

#define LOCAL_TRACE 0

#include <arch/arm64.h>
#define IFRAME_PC(frame) ((frame)->elr)

// values read from zbi
vaddr_t arm_gicv3_gic_base = 0;
uint64_t arm_gicv3_gicd_offset = 0;
uint64_t arm_gicv3_gicr_offset = 0;
uint64_t arm_gicv3_gicr_stride = 0;

//
// IMX8M Errata: e11171: CA53: Cannot support single-core runtime wakeup

// According to the GIC500 specification and the Arm Trusted Firmware design, when a CPU
// core enters the deepest CPU idle state (power-down), it must disable the GIC500 CPU
// interface and set the Redistributor register to indicate that this CPU is in sleep state.

// On NXP IMX8M, However, if the CPU core is in WFI or power-down with CPU interface disabled,
// another core cannot wake-up the powered-down core using SGI interrupt.

// One workaround is to use another A53 core for the IRQ0 which is controlled by the IOMUX
// GPR to generate an external interrupt to wake-up the powered-down core.
// The SW workaround is implemented into default BSP release. The workaround commit tag is
// “MLK-16804-04 driver: irqchip: Add IPI SW workaround for imx8mq" on the linux-imx project
static uint64_t mx8_gpr_virt = 0;

static uint32_t ipi_base = 0;

// this header uses the arm_gicv3_gic_* variables above
#include <dev/interrupt/arm_gicv3_regs.h>

static uint gic_max_int;

static bool gic_is_valid_interrupt(unsigned int vector, uint32_t flags) {
  return (vector < gic_max_int);
}

static uint32_t gic_get_base_vector() {
  // ARM Generic Interrupt Controller v3&4 chapter 2.2
  // INTIDs 0-15 are local CPU interrupts
  return 16;
}

static uint32_t gic_get_max_vector() { return gic_max_int; }

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
    for (cpu_num_t i = 0; i < arch_max_num_cpus(); i++) {
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

static void gic_init_percpu_early() {
  cpu_num_t cpu = arch_curr_cpu_num();

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
    DEBUG_ASSERT(sre & 0x1);
  }

  // set priority threshold to max.
  gic_write_pmr(0xff);

  // ICC_CTLR_EL1.EOImode.
  gic_write_ctlr(1u << 1);

  // enable group 1 interrupts.
  gic_write_igrpen(1);
}

static zx_status_t gic_init() {
  LTRACE_ENTRY;

  DEBUG_ASSERT(arch_ints_disabled());

  uint pidr2 = GICREG(0, GICD_PIDR2);
  uint rev = BITS_SHIFT(pidr2, 7, 4);
  if (rev != GICV3 && rev != GICV4) {
    return ZX_ERR_NOT_FOUND;
  }

  uint32_t typer = GICREG(0, GICD_TYPER);
  gic_max_int = (BITS(typer, 4, 0) + 1) * 32;

  printf("GICv3 detected, rev %u, max interrupts %u, TYPER %#x\n", rev, gic_max_int, typer);

  // disable the distributor
  GICREG(0, GICD_CTLR) = 0;
  gic_wait_for_rwp(GICD_CTLR);
  __isb(ARM_MB_SY);

  // distributor config: mask and clear all spis, set group 1.
  uint i;
  for (i = 32; i < gic_max_int; i += 32) {
    GICREG(0, GICD_ICENABLER(i / 32)) = ~0;
    GICREG(0, GICD_ICPENDR(i / 32)) = ~0;
    GICREG(0, GICD_IGROUPR(i / 32)) = ~0;
    GICREG(0, GICD_IGRPMODR(i / 32)) = 0;
  }
  gic_wait_for_rwp(GICD_CTLR);

  // enable distributor with ARE, group 1 enable
  GICREG(0, GICD_CTLR) = CTLR_ENABLE_G0 | CTLR_ENABLE_G1NS | CTLR_ARE_S;
  gic_wait_for_rwp(GICD_CTLR);

  // ensure we're running on cpu 0 and that cpu 0 corresponds to affinity 0.0.0.0
  DEBUG_ASSERT(arch_curr_cpu_num() == 0);
  DEBUG_ASSERT(arch_cpu_num_to_cpu_id(0u) == 0);      // AFF0
  DEBUG_ASSERT(arch_cpu_num_to_cluster_id(0u) == 0);  // AFF1

  // TODO(maniscalco): If/when we support AFF2/AFF3, be sure to assert those here.

  // set spi to target cpu 0 (affinity 0.0.0.0). must do this after ARE enable
  uint max_cpu = BITS_SHIFT(typer, 7, 5);
  if (max_cpu > 0) {
    for (i = 32; i < gic_max_int; i++) {
      GICREG64(0, GICD_IROUTER(i)) = 0;
    }
  }

  gic_init_percpu_early();

  arch::DeviceMemoryBarrier();
  __isb(ARM_MB_SY);

  return ZX_OK;
}

static zx_status_t arm_gic_sgi(unsigned int irq, unsigned int flags, unsigned int cpu_mask) {
  if (flags != ARM_GIC_SGI_FLAG_NS) {
    return ZX_ERR_INVALID_ARGS;
  }

  if (irq >= 16) {
    return ZX_ERR_INVALID_ARGS;
  }

  arch::ThreadMemoryBarrier();

  cpu_num_t cpu = 0;
  uint cluster = 0;
  uint64_t val = 0;
  while (cpu_mask && cpu < arch_max_num_cpus()) {
    unsigned int mask = 0;
    while (arch_cpu_num_to_cluster_id(cpu) == cluster) {
      if (cpu_mask & (1u << cpu)) {
        mask |= 1u << arch_cpu_num_to_cpu_id(cpu);
        cpu_mask &= ~(1u << cpu);
      }
      cpu += 1;
    }

    // Without the RS field set, we can only deal with the first
    // 16 cpus within a single cluster
    DEBUG_ASSERT((mask & 0xffff) == mask);

    val = ((irq & 0xf) << 24) | ((cluster & 0xff) << 16) | (mask & 0xffff);

    gic_write_sgi1r(val);
    cluster += 1;
    // Work around
    if (mx8_gpr_virt) {
      uint32_t regVal;
      // pending irq32 to wakeup core
      regVal = *(volatile uint32_t*)(mx8_gpr_virt + 0x4);
      regVal |= (1 << 12);
      *(volatile uint32_t*)(mx8_gpr_virt + 0x4) = regVal;
      // delay
      spin(50);
      regVal &= ~(1 << 12);
      *(volatile uint32_t*)(mx8_gpr_virt + 0x4) = regVal;
    }
  }

  return ZX_OK;
}

static zx_status_t gic_mask_interrupt(unsigned int vector) {
  LTRACEF("vector %u\n", vector);

  if (vector >= gic_max_int) {
    return ZX_ERR_INVALID_ARGS;
  }

  gic_set_enable(vector, false);

  return ZX_OK;
}

static zx_status_t gic_unmask_interrupt(unsigned int vector) {
  LTRACEF("vector %u\n", vector);

  if (vector >= gic_max_int) {
    return ZX_ERR_INVALID_ARGS;
  }

  gic_set_enable(vector, true);

  return ZX_OK;
}

static zx_status_t gic_deactivate_interrupt(unsigned int vector) {
  if (vector >= gic_max_int) {
    return ZX_ERR_INVALID_ARGS;
  }

  uint32_t reg = 1 << (vector % 32);
  GICREG(0, GICD_ICACTIVER(vector / 32)) = reg;

  return ZX_OK;
}

static zx_status_t gic_configure_interrupt(unsigned int vector, enum interrupt_trigger_mode tm,
                                           enum interrupt_polarity pol) {
  LTRACEF("vector %u, trigger mode %d, polarity %d\n", vector, tm, pol);

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

  const uint32_t clear_reg = vector / 32;
  const uint32_t clear_mask = 1 << (vector % 32);
  GICREG(0, GICD_ICPENDR(clear_reg)) = clear_mask;

  return ZX_OK;
}

static zx_status_t gic_get_interrupt_config(unsigned int vector, enum interrupt_trigger_mode* tm,
                                            enum interrupt_polarity* pol) {
  LTRACEF("vector %u\n", vector);

  if (vector >= gic_max_int) {
    return ZX_ERR_INVALID_ARGS;
  }

  if (tm) {
    *tm = IRQ_TRIGGER_MODE_EDGE;
  }
  if (pol) {
    *pol = IRQ_POLARITY_ACTIVE_HIGH;
  }

  return ZX_OK;
}

static unsigned int gic_remap_interrupt(unsigned int vector) {
  LTRACEF("vector %u\n", vector);
  return vector;
}

// called from assembly
static void gic_handle_irq(iframe_t* frame) {
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
  if (vector >= 32) {
    CPU_STATS_INC(interrupts);
  }

  cpu_num_t cpu = arch_curr_cpu_num();

  ktrace_tiny(TAG_IRQ_ENTER, (vector << 8) | cpu);

  LTRACEF_LEVEL(2, "iar 0x%x cpu %u currthread %p vector %u pc %#" PRIxPTR "\n", iar, cpu,
                Thread::Current::Get(), vector, (uintptr_t)IFRAME_PC(frame));

  // deliver the interrupt
  interrupt_eoi eoi;
  if (!pdev_invoke_int_if_present(vector, &eoi)) {
    eoi = IRQ_EOI_DEACTIVATE;
  }
  gic_write_eoir(vector);
  if (eoi == IRQ_EOI_DEACTIVATE) {
    gic_write_dir(vector);
  }

  LTRACEF_LEVEL(2, "cpu %u exit\n", cpu);

  ktrace_tiny(TAG_IRQ_EXIT, (vector << 8) | cpu);
}

static void gic_handle_fiq(iframe_t* frame) { PANIC_UNIMPLEMENTED; }

static void gic_send_ipi(cpu_mask_t target, mp_ipi_t ipi) {
  uint gic_ipi_num = ipi + ipi_base;

  // filter out targets outside of the range of cpus we care about
  target &= (cpu_mask_t)(((1UL << arch_max_num_cpus()) - 1));
  if (target != 0) {
    LTRACEF("target 0x%x, gic_ipi %u\n", target, gic_ipi_num);
    arm_gic_sgi(gic_ipi_num, ARM_GIC_SGI_FLAG_NS, target);
  }
}

static interrupt_eoi arm_ipi_halt_handler(void*) {
  LTRACEF("cpu %u\n", arch_curr_cpu_num());

  arch_disable_ints();
  while (true) {
  }

  return IRQ_EOI_DEACTIVATE;
}

static void gic_init_percpu() {
  mp_set_curr_cpu_online(true);
  unmask_interrupt(MP_IPI_GENERIC + ipi_base);
  unmask_interrupt(MP_IPI_RESCHEDULE + ipi_base);
  unmask_interrupt(MP_IPI_INTERRUPT + ipi_base);
  unmask_interrupt(MP_IPI_HALT + ipi_base);
}

static void gic_shutdown() {
  // Turn off all GIC0 interrupts at the distributor.
  GICREG(0, GICD_CTLR) = 0;
}

// Returns true if any PPIs are enabled on the calling CPU.
static bool is_ppi_enabled() {
  DEBUG_ASSERT(arch_ints_disabled());

  // PPIs are 16-31.
  uint32_t mask = 0xffff0000;

  cpu_num_t cpu_num = arch_curr_cpu_num();
  uint32_t reg = GICREG(0, GICR_ICENABLER0(cpu_num));
  if ((reg & mask) != 0) {
    return true;
  }

  return false;
}

// Returns true if any SPIs are enabled on the calling CPU.
static bool is_spi_enabled() {
  DEBUG_ASSERT(arch_ints_disabled());

  cpu_num_t cpu_num = arch_curr_cpu_num();

  // TODO(maniscalco): If/when we support AFF2/AFF3, update the mask below.
  uint aff0 = arch_cpu_num_to_cpu_id(cpu_num);
  uint aff1 = arch_cpu_num_to_cluster_id(cpu_num);
  uint64_t aff_mask = (aff1 << 8) + aff0;

  // Check each SPI to see if it's routed to this CPU.
  for (uint i = 32u; i < gic_max_int; ++i) {
    if ((GICREG64(0, GICD_IROUTER(i)) & aff_mask) != 0) {
      return true;
    }
  }

  return false;
}

static void gic_shutdown_cpu() {
  DEBUG_ASSERT(arch_ints_disabled());

  // If we're running on a secondary CPU there's a good chance this CPU will be powered off shortly
  // (PSCI_CPU_OFF).  Sending an interrupt to a CPU that's been powered off may result in an
  // "erronerous state" (see Power State Coordination Interface (PSCI) System Software on ARM
  // specification, 5.5.2).  So before we shutdown the GIC, make sure we've migrated/disabled any
  // and all peripheral interrupts targeted at this CPU (PPIs and SPIs).
  //
  // Note, we don't perform these checks on the boot CPU because we don't call PSCI_CPU_OFF on the
  // boot CPU, and we likely still have PPIs and SPIs targeting the boot CPU.
  DEBUG_ASSERT(arch_curr_cpu_num() == BOOT_CPU_ID || !is_ppi_enabled());
  DEBUG_ASSERT(arch_curr_cpu_num() == BOOT_CPU_ID || !is_spi_enabled());
  // TODO(maniscalco): If/when we start using LPIs, make sure none are targeted at this CPU.

  // Disable group 1 interrupts at the CPU interface.
  gic_write_igrpen(0);
}

static bool gic_msi_is_supported() { return false; }

static bool gic_msi_supports_masking() { return false; }

static void gic_msi_mask_unmask(const msi_block_t* block, uint msi_id, bool mask) {
  PANIC_UNIMPLEMENTED;
}

static zx_status_t gic_msi_alloc_block(uint requested_irqs, bool can_target_64bit, bool is_msix,
                                       msi_block_t* out_block) {
  PANIC_UNIMPLEMENTED;
}

static void gic_msi_free_block(msi_block_t* block) { PANIC_UNIMPLEMENTED; }

static void gic_msi_register_handler(const msi_block_t* block, uint msi_id, int_handler handler,
                                     void* ctx) {
  PANIC_UNIMPLEMENTED;
}

static const struct pdev_interrupt_ops gic_ops = {
    .mask = gic_mask_interrupt,
    .unmask = gic_unmask_interrupt,
    .deactivate = gic_deactivate_interrupt,
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
    .shutdown_cpu = gic_shutdown_cpu,
    .msi_is_supported = gic_msi_is_supported,
    .msi_supports_masking = gic_msi_supports_masking,
    .msi_mask_unmask = gic_msi_mask_unmask,
    .msi_alloc_block = gic_msi_alloc_block,
    .msi_free_block = gic_msi_free_block,
    .msi_register_handler = gic_msi_register_handler,
};

static void arm_gic_v3_init_early(const void* driver_data, uint32_t length) {
  ASSERT(length >= sizeof(dcfg_arm_gicv3_driver_t));
  auto driver = static_cast<const dcfg_arm_gicv3_driver_t*>(driver_data);
  ASSERT(driver->mmio_phys);

  LTRACE_ENTRY;

  // If a GIC driver is already registered to the GIC interface it's means we are running GICv2
  // and we do not need to initialize GICv3. Since we have added both GICv3 and GICv2 in board.mdi,
  // both drivers are initialized
  if (arm_gic_is_registered()) {
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

  zx_status_t status = gic_register_sgi_handler(MP_IPI_GENERIC + ipi_base, &mp_mbx_generic_irq);
  DEBUG_ASSERT(status == ZX_OK);
  status = gic_register_sgi_handler(MP_IPI_RESCHEDULE + ipi_base, &mp_mbx_reschedule_irq);
  DEBUG_ASSERT(status == ZX_OK);
  status = gic_register_sgi_handler(MP_IPI_INTERRUPT + ipi_base, &mp_mbx_interrupt_irq);
  DEBUG_ASSERT(status == ZX_OK);
  status = gic_register_sgi_handler(MP_IPI_HALT + ipi_base, &arm_ipi_halt_handler);
  DEBUG_ASSERT(status == ZX_OK);

  gicv3_hw_interface_register();

  LTRACE_EXIT;
}

void arm_gic_v3_init_deny_regions(const void* driver_data, uint32_t length) {
  // Place the physical address of the GICv3 registers on the MMIO deny list.
  // Users will not be able to create MMIO resources which permit mapping of the
  // GIC registers, even if they have access to the root resource.
  //
  // Unlike GICv2, only the distributor and re-distributor registers are memory
  // mapped. There is one block of distributor registers for the system, and
  // one block of redistributor registers for each CPU.
  ASSERT(length >= sizeof(dcfg_arm_gicv3_driver_t));
  auto driver = static_cast<const dcfg_arm_gicv3_driver_t*>(driver_data);
  ASSERT(driver->mmio_phys);

  root_resource_filter_add_deny_region(driver->mmio_phys + driver->gicd_offset, GICD_REG_SIZE,
                                       ZX_RSRC_KIND_MMIO);
  for (cpu_num_t i = 0; i < arch_max_num_cpus(); i++) {
    root_resource_filter_add_deny_region(
        driver->mmio_phys + driver->gicr_offset + (driver->gicr_stride * i), GICR_REG_SIZE,
        ZX_RSRC_KIND_MMIO);
  }
}

LK_PDEV_INIT(arm_gic_v3_init_early, KDRV_ARM_GIC_V3, arm_gic_v3_init_early,
             LK_INIT_LEVEL_PLATFORM_EARLY)
LK_PDEV_INIT(arm_gic_v3_init, KDRV_ARM_GIC_V3, arm_gic_v3_init_deny_regions,
             LK_INIT_LEVEL_PLATFORM + 1)
