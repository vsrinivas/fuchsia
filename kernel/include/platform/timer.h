// Copyright 2016 The Fuchsia Authors
// Copyright (c) 2008 Travis Geiselbrecht
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#pragma once

#include <sys/types.h>
#include <zircon/compiler.h>
#include <zircon/types.h>

__BEGIN_CDECLS

typedef void(*platform_timer_callback)(zx_time_t now);

// API to set/clear a hardware timer that is responsible for calling timer_tick() when it fires
zx_status_t platform_set_oneshot_timer(zx_time_t deadline);
void        platform_stop_timer(void);

void timer_tick(zx_time_t now);

__END_CDECLS
