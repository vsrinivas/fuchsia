// Copyright 2017 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#pragma once

#include <dev/interrupt.h>
#include <zircon/compiler.h>
#include <zircon/types.h>

__BEGIN_CDECLS

struct int_handler_struct {
    int_handler handler;
    void* arg;
};

struct int_handler_struct* pdev_get_int_handler(unsigned int vector);

// Interrupt Controller interface
struct pdev_interrupt_ops {
    zx_status_t (*mask)(unsigned int vector);
    zx_status_t (*unmask)(unsigned int vector);
    zx_status_t (*configure)(unsigned int vector,
                             enum interrupt_trigger_mode tm,
                             enum interrupt_polarity pol);
    zx_status_t (*get_config)(unsigned int vector,
                              enum interrupt_trigger_mode* tm,
                              enum interrupt_polarity* pol);
    bool (*is_valid)(unsigned int vector, uint32_t flags);
    uint32_t (*get_base_vector)(void);
    uint32_t (*get_max_vector)(void);
    unsigned int (*remap)(unsigned int vector);
    zx_status_t (*send_ipi)(cpu_mask_t target, mp_ipi_t ipi);
    void (*init_percpu_early)(void);
    void (*init_percpu)(void);
    void (*handle_irq)(iframe* frame);
    void (*handle_fiq)(iframe* frame);
    void (*shutdown)(void);
};

void pdev_register_interrupts(const struct pdev_interrupt_ops* ops);

__END_CDECLS
