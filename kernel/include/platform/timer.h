// Copyright 2016 The Fuchsia Authors
// Copyright (c) 2008 Travis Geiselbrecht
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#pragma once

#include <magenta/compiler.h>
#include <sys/types.h>

__BEGIN_CDECLS

typedef enum handler_return(*platform_timer_callback)(lk_time_t now);

// API to set/clear a hardware timer that is responsible for calling timer_tick() when it fires
status_t platform_set_oneshot_timer(lk_time_t deadline);
void     platform_stop_timer(void);

enum handler_return timer_tick(lk_time_t now);

__END_CDECLS
