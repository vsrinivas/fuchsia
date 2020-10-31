// Copyright 2020 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <assert.h>
#include <bits.h>
#include <reg.h>
#include <inttypes.h>
#include <lib/arch/intrin.h>
#include <lib/ktrace.h>
#include <lib/root_resource_filter.h>
#include <string.h>
#include <trace.h>
#include <zircon/errors.h>
#include <zircon/boot/driver-config.h>
#include <zircon/types.h>

#include <dev/interrupt.h>
#include <kernel/cpu.h>
#include <kernel/stats.h>
#include <kernel/thread.h>
#include <lk/init.h>
#include <pdev/driver.h>
#include <pdev/interrupt.h>
#include <vm/vm.h>
#include <vm/physmap.h>
#include <arch/regs.h>
#include <arch/riscv64/mp.h>

// Driver for PLIC implementation for qemu riscv virt machine
#define PLIC_HART_IDX(hart)    ((2 * (hart)) + 1)

#define PLIC_PRIORITY(plic_base, irq)     (plic_base + 4 + 4 * (irq))
#define PLIC_PENDING(plic_base, irq)      (plic_base + 0x1000 + (4 * ((irq) / 32)))
#define PLIC_ENABLE(plic_base, irq, hart) (plic_base + 0x2000 + (0x80 * PLIC_HART_IDX(hart)) + (4 * ((irq) / 32)))
#define PLIC_THRESHOLD(plic_base, hart)   (plic_base + 0x200000 + (0x1000 * PLIC_HART_IDX(hart)))
#define PLIC_COMPLETE(plic_base, hart)    (plic_base + 0x200004 + (0x1000 * PLIC_HART_IDX(hart)))
#define PLIC_CLAIM(plic_base, hart)       PLIC_COMPLETE(plic_base, hart)

#define LOCAL_TRACE 0

#include <arch/riscv64.h>

// values read from zbi
vaddr_t plic_base = 0;
static uint plic_max_int = 0;

static bool plic_is_valid_interrupt(unsigned int vector, uint32_t flags) {
  return (vector < plic_max_int);
}

static uint32_t plic_get_base_vector() {
  return 0;
}

static uint32_t plic_get_max_vector() { return plic_max_int; }


static void plic_init_percpu_early() {
}

static zx_status_t plic_mask_interrupt(unsigned int vector) {
  LTRACEF("vector %u\n", vector);

  if (vector >= plic_max_int) {
    return ZX_ERR_INVALID_ARGS;
  }

  *REG32(PLIC_ENABLE(plic_base, vector, riscv64_curr_hart_id())) &= ~(1 << (vector % 32));

  return ZX_OK;
}

static zx_status_t plic_unmask_interrupt(unsigned int vector) {
  LTRACEF("vector %u\n", vector);

  if (vector >= plic_max_int) {
    return ZX_ERR_INVALID_ARGS;
  }

  *REG32(PLIC_ENABLE(plic_base, vector, riscv64_curr_hart_id())) |= (1 << (vector % 32));

  return ZX_OK;
}

static zx_status_t plic_deactivate_interrupt(unsigned int vector){
  if (vector >= plic_max_int) {
    return ZX_ERR_INVALID_ARGS;
  }

  // TODO(revest): deactivate

  return ZX_OK;
}

static zx_status_t plic_configure_interrupt(unsigned int vector, enum interrupt_trigger_mode tm,
                                            enum interrupt_polarity pol) {
  LTRACEF("vector %u, trigger mode %d, polarity %d\n", vector, tm, pol);

  if (vector >= plic_max_int) {
    return ZX_ERR_INVALID_ARGS;
  }

  if (pol != IRQ_POLARITY_ACTIVE_HIGH) {
    return ZX_ERR_NOT_SUPPORTED;
  }

  return ZX_OK;
}

static zx_status_t plic_get_interrupt_config(unsigned int vector, enum interrupt_trigger_mode* tm,
                                             enum interrupt_polarity* pol) {
  LTRACEF("vector %u\n", vector);

  if (vector >= plic_max_int) {
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

static unsigned int plic_remap_interrupt(unsigned int vector) {
  LTRACEF("vector %u\n", vector);
  return vector;
}

static void plic_handle_irq(iframe_t* frame) {
  // get the current vector
  uint32_t vector = *REG32(PLIC_CLAIM(plic_base, riscv64_curr_hart_id()));
  LTRACEF_LEVEL(2, "vector %u\n", vector);

  if (unlikely(vector == 0)) {
    // spurious
    // TODO check this
    return;
  }

  cpu_num_t cpu = arch_curr_cpu_num();

  ktrace_tiny(TAG_IRQ_ENTER, (vector << 8) | cpu);

  LTRACEF_LEVEL(2, "cpu %u currthread %p vector %u pc %#" PRIxPTR "\n", cpu,
                Thread::Current::Get(), vector, (uintptr_t)frame->epc);

  // deliver the interrupt
  interrupt_eoi eoi;
  if (!pdev_invoke_int_if_present(vector, &eoi)) {
    eoi = IRQ_EOI_DEACTIVATE;
  }
  if (eoi == IRQ_EOI_DEACTIVATE) {
    *REG32(PLIC_COMPLETE(plic_base, riscv64_curr_hart_id())) = vector;
  }

  LTRACEF_LEVEL(2, "cpu %u exit\n", cpu);

  ktrace_tiny(TAG_IRQ_EXIT, (vector << 8) | cpu);
}

static void plic_handle_fiq(iframe_t* frame) { PANIC_UNIMPLEMENTED; }

static void plic_send_ipi(cpu_mask_t target, mp_ipi_t ipi) { PANIC_UNIMPLEMENTED; }

static void plic_init_percpu() {
}

static void plic_shutdown() {
  PANIC_UNIMPLEMENTED;
}

static void plic_shutdown_cpu() {
  PANIC_UNIMPLEMENTED;
}

static bool plic_msi_is_supported() { return false; }

static bool plic_msi_supports_masking() { return false; }

static void plic_msi_mask_unmask(const msi_block_t* block, uint msi_id, bool mask) {
  PANIC_UNIMPLEMENTED;
}

static zx_status_t plic_msi_alloc_block(uint requested_irqs, bool can_target_64bit, bool is_msix,
                                       msi_block_t* out_block) {
  PANIC_UNIMPLEMENTED;
}

static void plic_msi_free_block(msi_block_t* block) { PANIC_UNIMPLEMENTED; }

static void plic_msi_register_handler(const msi_block_t* block, uint msi_id, int_handler handler,
                                     void* ctx) {
  PANIC_UNIMPLEMENTED;
}

static const struct pdev_interrupt_ops plic_ops = {
    .mask = plic_mask_interrupt,
    .unmask = plic_unmask_interrupt,
    .deactivate = plic_deactivate_interrupt,
    .configure = plic_configure_interrupt,
    .get_config = plic_get_interrupt_config,
    .is_valid = plic_is_valid_interrupt,
    .get_base_vector = plic_get_base_vector,
    .get_max_vector = plic_get_max_vector,
    .remap = plic_remap_interrupt,
    .send_ipi = plic_send_ipi,
    .init_percpu_early = plic_init_percpu_early,
    .init_percpu = plic_init_percpu,
    .handle_irq = plic_handle_irq,
    .handle_fiq = plic_handle_fiq,
    .shutdown = plic_shutdown,
    .shutdown_cpu = plic_shutdown_cpu,
    .msi_is_supported = plic_msi_is_supported,
    .msi_supports_masking = plic_msi_supports_masking,
    .msi_mask_unmask = plic_msi_mask_unmask,
    .msi_alloc_block = plic_msi_alloc_block,
    .msi_free_block = plic_msi_free_block,
    .msi_register_handler = plic_msi_register_handler,
};

static void riscv_plic_init_early(const void* driver_data, uint32_t length) {
  ASSERT(length >= sizeof(dcfg_riscv_plic_driver_t));
  auto driver = static_cast<const dcfg_riscv_plic_driver_t*>(driver_data);
  ASSERT(driver->mmio_phys);

  LTRACE_ENTRY;

  plic_base = (vaddr_t)paddr_to_physmap(driver->mmio_phys);
  plic_max_int = driver->num_irqs;
  ASSERT(plic_base && plic_max_int);

  pdev_register_interrupts(&plic_ops);

  // mask all irqs and set their priority to 1
  // TODO: mask on all the other cpus too
  for (uint i = 1; i < plic_max_int; i++) {
    *REG32(PLIC_ENABLE(plic_base, i, riscv64_curr_hart_id())) &= ~(1 << (i % 32));
    *REG32(PLIC_PRIORITY(plic_base, i)) = 1;
  }

  // set global priority threshold to 0
  *REG32(PLIC_THRESHOLD(plic_base, riscv64_curr_hart_id())) = 0;

  LTRACE_EXIT;
}

LK_PDEV_INIT(riscv_plic_init_early, KDRV_RISCV_PLIC, riscv_plic_init_early,
             LK_INIT_LEVEL_PLATFORM_EARLY)
