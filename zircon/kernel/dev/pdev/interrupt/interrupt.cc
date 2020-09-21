// Copyright 2017 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <err.h>
#include <lib/arch/intrin.h>
#include <zircon/types.h>

#include <kernel/auto_lock.h>
#include <kernel/spinlock.h>
#include <lk/init.h>
#include <pdev/interrupt.h>

#define ARM_MAX_INT 1024

DECLARE_SINGLETON_SPINLOCK(pdev_lock);

struct int_handler_struct {
  int_handler handler TA_GUARDED(pdev_lock::Get()) = nullptr;
  void* arg TA_GUARDED(pdev_lock::Get()) = nullptr;
  ktl::atomic<bool> permanent = false;
};

static struct int_handler_struct int_handler_table[ARM_MAX_INT];

static struct int_handler_struct* pdev_get_int_handler(unsigned int vector) {
  DEBUG_ASSERT(vector < ARM_MAX_INT);
  return &int_handler_table[vector];
}

bool pdev_invoke_int_if_present(unsigned int vector, interrupt_eoi* result) {
  auto h = pdev_get_int_handler(vector);
  // Use a relaxed load as permanent handlers are never modified once set, and they are only set in
  // startup code, and so there is nothing to race with.
  if (h->permanent.load(ktl::memory_order_relaxed)) {
    // Once permanent is set to true we know that handler and arg are immutable and so it is safe
    // to read them without holding the lock.
    [&result, &h]() TA_NO_THREAD_SAFETY_ANALYSIS {
      DEBUG_ASSERT(h->handler);
      *result = h->handler(h->arg);
    }();
    return true;
  }
  Guard<SpinLock, IrqSave> guard{pdev_lock::Get()};

  if (h->handler) {
    *result = h->handler(h->arg);
    return true;
  }
  return false;
}

static zx_status_t register_int_handler_common(unsigned int vector, int_handler handler, void* arg,
                                               bool permanent) {
  if (!is_valid_interrupt(vector, 0)) {
    return ZX_ERR_INVALID_ARGS;
  }

  Guard<SpinLock, IrqSave> guard{pdev_lock::Get()};

  auto h = pdev_get_int_handler(vector);
  if ((handler && h->handler) || h->permanent.load(ktl::memory_order_relaxed)) {
    return ZX_ERR_ALREADY_BOUND;
  }
  h->handler = handler;
  h->arg = arg;
  h->permanent.store(permanent, ktl::memory_order_relaxed);

  return ZX_OK;
}

zx_status_t register_int_handler(unsigned int vector, int_handler handler, void* arg) {
  return register_int_handler_common(vector, handler, arg, false);
}

zx_status_t register_permanent_int_handler(unsigned int vector, int_handler handler, void* arg) {
  return register_int_handler_common(vector, handler, arg, true);
}

static zx_status_t default_mask(unsigned int vector) { return ZX_ERR_NOT_SUPPORTED; }

static zx_status_t default_unmask(unsigned int vector) { return ZX_ERR_NOT_SUPPORTED; }

static zx_status_t default_deactivate(unsigned int vector) { return ZX_ERR_NOT_SUPPORTED; }

static zx_status_t default_configure(unsigned int vector, enum interrupt_trigger_mode tm,
                                     enum interrupt_polarity pol) {
  return ZX_ERR_NOT_SUPPORTED;
}

static zx_status_t default_get_config(unsigned int vector, enum interrupt_trigger_mode* tm,
                                      enum interrupt_polarity* pol) {
  return ZX_ERR_NOT_SUPPORTED;
}

static bool default_is_valid(unsigned int vector, uint32_t flags) { return false; }
static unsigned int default_remap(unsigned int vector) { return 0; }

static void default_send_ipi(cpu_mask_t target, mp_ipi_t ipi) {}

static void default_init_percpu_early() {}

static void default_init_percpu() {}

static void default_handle_irq(iframe_t* frame) {}

static void default_handle_fiq(iframe_t* frame) {}

static void default_shutdown() {}

static void default_shutdown_cpu() {}

static bool default_msi_is_supported() { return false; }

static bool default_msi_supports_masking() { return false; }

static zx_status_t default_msi_alloc_block(uint requested_irqs, bool can_target_64bit, bool is_msix,
                                           msi_block_t* out_block) {
  return ZX_ERR_NOT_SUPPORTED;
}

static void default_msi_free_block(msi_block_t* block) {}

static void default_msi_register_handler(const msi_block_t* block, uint msi_id, int_handler handler,
                                         void* ctx) {}

static void default_msi_mask_unmask(const msi_block_t* block, uint msi_id, bool mask) {}

static uint32_t default_get_base_vector() { return 0; }

static uint32_t default_get_max_vector() { return 0; }

// by default, most interrupt operations for pdev/arm are implemented in the gic specific source
// files and accessed via configuring this pointer table at runtime. By default most of these
// are merely empty stubs.
static const struct pdev_interrupt_ops default_ops = {
    .mask = default_mask,
    .unmask = default_unmask,
    .deactivate = default_deactivate,
    .configure = default_configure,
    .get_config = default_get_config,
    .is_valid = default_is_valid,
    .get_base_vector = default_get_base_vector,
    .get_max_vector = default_get_max_vector,
    .remap = default_remap,
    .send_ipi = default_send_ipi,
    .init_percpu_early = default_init_percpu_early,
    .init_percpu = default_init_percpu,
    .handle_irq = default_handle_irq,
    .handle_fiq = default_handle_fiq,
    .shutdown = default_shutdown,
    .shutdown_cpu = default_shutdown_cpu,
    .msi_is_supported = default_msi_is_supported,
    .msi_supports_masking = default_msi_supports_masking,
    .msi_mask_unmask = default_msi_mask_unmask,
    .msi_alloc_block = default_msi_alloc_block,
    .msi_free_block = default_msi_free_block,
    .msi_register_handler = default_msi_register_handler,
};

static const struct pdev_interrupt_ops* intr_ops = &default_ops;

zx_status_t mask_interrupt(unsigned int vector) { return intr_ops->mask(vector); }

zx_status_t unmask_interrupt(unsigned int vector) { return intr_ops->unmask(vector); }

zx_status_t deactivate_interrupt(unsigned int vector) { return intr_ops->deactivate(vector); }

zx_status_t configure_interrupt(unsigned int vector, enum interrupt_trigger_mode tm,
                                enum interrupt_polarity pol) {
  return intr_ops->configure(vector, tm, pol);
}

zx_status_t get_interrupt_config(unsigned int vector, enum interrupt_trigger_mode* tm,
                                 enum interrupt_polarity* pol) {
  return intr_ops->get_config(vector, tm, pol);
}

uint32_t interrupt_get_base_vector() { return intr_ops->get_base_vector(); }

uint32_t interrupt_get_max_vector() { return intr_ops->get_max_vector(); }

bool is_valid_interrupt(unsigned int vector, uint32_t flags) {
  return intr_ops->is_valid(vector, flags);
}

unsigned int remap_interrupt(unsigned int vector) { return intr_ops->remap(vector); }

void interrupt_send_ipi(cpu_mask_t target, mp_ipi_t ipi) { intr_ops->send_ipi(target, ipi); }

void interrupt_init_percpu() { intr_ops->init_percpu(); }

void platform_irq(iframe_t* frame) { intr_ops->handle_irq(frame); }

void platform_fiq(iframe_t* frame) { intr_ops->handle_fiq(frame); }

void pdev_register_interrupts(const struct pdev_interrupt_ops* ops) {
  intr_ops = ops;
  arch::ThreadMemoryBarrier();
}

static void interrupt_init_percpu_early(uint level) { intr_ops->init_percpu_early(); }

void shutdown_interrupts() { intr_ops->shutdown(); }

void shutdown_interrupts_curr_cpu() { intr_ops->shutdown_cpu(); }

bool msi_is_supported() { return intr_ops->msi_is_supported(); }

bool msi_supports_masking() { return intr_ops->msi_supports_masking(); }

void msi_mask_unmask(const msi_block_t* block, uint msi_id, bool mask) {
  intr_ops->msi_mask_unmask(block, msi_id, mask);
}

zx_status_t msi_alloc_block(uint requested_irqs, bool can_target_64bit, bool is_msix,
                            msi_block_t* out_block) {
  return intr_ops->msi_alloc_block(requested_irqs, can_target_64bit, is_msix, out_block);
}

void msi_free_block(msi_block_t* block) { intr_ops->msi_free_block(block); }

void msi_register_handler(const msi_block_t* block, uint msi_id, int_handler handler, void* ctx) {
  intr_ops->msi_register_handler(block, msi_id, handler, ctx);
}

LK_INIT_HOOK_FLAGS(interrupt_init_percpu_early, interrupt_init_percpu_early,
                   LK_INIT_LEVEL_PLATFORM_EARLY, LK_INIT_FLAG_SECONDARY_CPUS)
