// Copyright 2017 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <err.h>
#include <pdev/interrupt.h>
#include <lk/init.h>

#define ARM_MAX_INT 1024

static spin_lock_t lock = SPIN_LOCK_INITIAL_VALUE;

static struct int_handler_struct int_handler_table[ARM_MAX_INT];

struct int_handler_struct* pdev_get_int_handler(unsigned int vector)
{
    DEBUG_ASSERT(vector < ARM_MAX_INT);
    return &int_handler_table[vector];
}

void register_int_handler(unsigned int vector, int_handler handler, void* arg)
{
    struct int_handler_struct *h;

    spin_lock_saved_state_t state;

    if (!is_valid_interrupt(vector, 0)) {
        panic("register_int_handler: vector out of range %u\n", vector);
    }

    spin_lock_save(&lock, &state, SPIN_LOCK_FLAG_INTERRUPTS);

    h = pdev_get_int_handler(vector);
    h->handler = handler;
    h->arg = arg;

    spin_unlock_restore(&lock, state, SPIN_LOCK_FLAG_INTERRUPTS);
}

static status_t default_mask(unsigned int vector) {
    return MX_ERR_NOT_CONFIGURED;
}

static status_t default_unmask(unsigned int vector) {
    return MX_ERR_NOT_CONFIGURED;
}

static status_t default_configure(unsigned int vector,
                          enum interrupt_trigger_mode tm,
                          enum interrupt_polarity pol) {
    return MX_ERR_NOT_CONFIGURED;
}

static status_t default_get_config(unsigned int vector,
                           enum interrupt_trigger_mode* tm,
                           enum interrupt_polarity* pol) {
    return MX_ERR_NOT_CONFIGURED;
}

static bool default_is_valid(unsigned int vector, uint32_t flags) {
    return false;
}
static unsigned int default_remap(unsigned int vector) {
    return 0;
}

static status_t default_send_ipi(mp_cpu_mask_t target, mp_ipi_t ipi) {
    return MX_ERR_NOT_CONFIGURED;
}

static void default_init_percpu_early(void) {
}

static void default_init_percpu(void) {
}

static enum handler_return default_handle_irq(iframe* frame) {
    return INT_NO_RESCHEDULE;
}

static enum handler_return default_handle_fiq(iframe* frame) {
    return INT_NO_RESCHEDULE;
}

static void default_shutdown(void) {
}

static const struct pdev_interrupt_ops default_ops = {
    .mask = default_mask,
    .unmask = default_unmask,
    .configure = default_configure,
    .get_config = default_get_config,
    .is_valid = default_is_valid,
    .remap = default_remap,
    .send_ipi = default_send_ipi,
    .init_percpu_early = default_init_percpu_early,
    .init_percpu = default_init_percpu,
    .handle_irq = default_handle_irq,
    .handle_fiq = default_handle_fiq,
    .shutdown = default_shutdown,
};

static const struct pdev_interrupt_ops* intr_ops = &default_ops;

status_t mask_interrupt(unsigned int vector) {
    return intr_ops->mask(vector);
}

status_t unmask_interrupt(unsigned int vector) {
    return intr_ops->unmask(vector);
}

status_t configure_interrupt(unsigned int vector, enum interrupt_trigger_mode tm,
                             enum interrupt_polarity pol) {
    return intr_ops->configure(vector, tm, pol);
}

status_t get_interrupt_config(unsigned int vector, enum interrupt_trigger_mode* tm,
                              enum interrupt_polarity* pol) {
    return intr_ops->get_config(vector, tm, pol);
}

bool is_valid_interrupt(unsigned int vector, uint32_t flags) {
    return intr_ops->is_valid(vector, flags);
}

unsigned int remap_interrupt(unsigned int vector) {
    return intr_ops->remap(vector);
}

status_t interrupt_send_ipi(mp_cpu_mask_t target, mp_ipi_t ipi) {
    return intr_ops->send_ipi(target, ipi);
}

void interrupt_init_percpu(void) {
    intr_ops->init_percpu();
}

enum handler_return platform_irq(iframe* frame) {
    return intr_ops->handle_irq(frame);
}

enum handler_return platform_fiq(iframe* frame) {
    return intr_ops->handle_fiq(frame);
}

void pdev_register_interrupts(const struct pdev_interrupt_ops* ops) {
    intr_ops = ops;
    smp_mb();
}

static void interrupt_init_percpu_early(uint level) {
    intr_ops->init_percpu_early();
}

void shutdown_interrupts(void) {
    intr_ops->shutdown();
}

LK_INIT_HOOK_FLAGS(interrupt_init_percpu_early, interrupt_init_percpu_early, LK_INIT_LEVEL_PLATFORM_EARLY, LK_INIT_FLAG_SECONDARY_CPUS);
