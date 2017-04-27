// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#pragma once

#include <kernel/mp.h>
#include <magenta/compiler.h>
#include <stdbool.h>
#include <sys/types.h>

__BEGIN_CDECLS

enum interrupt_trigger_mode {
    IRQ_TRIGGER_MODE_EDGE = 0,
    IRQ_TRIGGER_MODE_LEVEL = 1,
};

enum interrupt_polarity {
    IRQ_POLARITY_ACTIVE_HIGH = 0,
    IRQ_POLARITY_ACTIVE_LOW = 1,
};

status_t mask_interrupt(unsigned int vector);
status_t unmask_interrupt(unsigned int vector);
void shutdown_interrupts(void);

// Configure the specified interrupt vector.  If it is invoked, it muust be
// invoked prior to interrupt registration
status_t configure_interrupt(unsigned int vector,
                             enum interrupt_trigger_mode tm,
                             enum interrupt_polarity pol);

status_t get_interrupt_config(unsigned int vector,
                              enum interrupt_trigger_mode* tm,
                              enum interrupt_polarity* pol);

typedef enum handler_return (*int_handler)(void* arg);

void register_int_handler(unsigned int vector, int_handler handler, void* arg);

bool is_valid_interrupt(unsigned int vector, uint32_t flags);

unsigned int remap_interrupt(unsigned int vector);

/* sends an inter-processor interrupt */
status_t interrupt_send_ipi(mp_cpu_mask_t target, mp_ipi_t ipi);

/* performs per-cpu initialization for the interrupt controller */
void interrupt_init_percpu(void);

__END_CDECLS
