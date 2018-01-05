// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#pragma once

#include <platform/pc/acpi.h>
#include <zircon/compiler.h>
#include <zircon/types.h>

__BEGIN_CDECLS

bool hpet_is_present(void);

static uint64_t hpet_ticks_per_ms(void) {
    extern uint64_t _hpet_ticks_per_ms;
    return _hpet_ticks_per_ms;
}

uint64_t hpet_get_value(void);
zx_status_t hpet_set_value(uint64_t v);

zx_status_t hpet_timer_configure_irq(uint n, uint irq);
zx_status_t hpet_timer_set_oneshot(uint n, uint64_t deadline);
zx_status_t hpet_timer_set_periodic(uint n, uint64_t period);
zx_status_t hpet_timer_disable(uint n);

void hpet_enable(void);
void hpet_disable(void);

void hpet_wait_ms(uint16_t ms);

__END_CDECLS
