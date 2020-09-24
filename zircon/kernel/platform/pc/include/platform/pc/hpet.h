// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#ifndef ZIRCON_KERNEL_PLATFORM_PC_INCLUDE_PLATFORM_PC_HPET_H_
#define ZIRCON_KERNEL_PLATFORM_PC_INCLUDE_PLATFORM_PC_HPET_H_

#include <lib/acpi_tables.h>
#include <zircon/compiler.h>
#include <zircon/types.h>

__BEGIN_CDECLS

bool hpet_is_present(void);

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

#ifdef __cplusplus
#include <lib/affine/ratio.h>

// Storage resides in platform/pc/timer.cpp
extern affine::Ratio hpet_ticks_to_clock_monotonic;
#endif  // __cplusplus

#endif  // ZIRCON_KERNEL_PLATFORM_PC_INCLUDE_PLATFORM_PC_HPET_H_
