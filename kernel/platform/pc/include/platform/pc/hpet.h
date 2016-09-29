// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#pragma once

#include <magenta/compiler.h>
#include <platform/pc/acpi.h>

__BEGIN_CDECLS

bool hpet_is_present(void);

static uint64_t hpet_ticks_per_ms(void) {
    extern uint64_t _hpet_ticks_per_ms;
    return _hpet_ticks_per_ms;
}

uint64_t hpet_get_value(void);
status_t hpet_set_value(uint64_t v);

status_t hpet_timer_configure_irq(uint n, uint irq);
status_t hpet_timer_set_oneshot(uint n, uint64_t deadline);
status_t hpet_timer_set_periodic(uint n, uint64_t period);
status_t hpet_timer_disable(uint n);

void hpet_enable(void);
void hpet_disable(void);

void hpet_wait_ms(uint16_t ms);

__END_CDECLS
