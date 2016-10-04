// Copyright 2016 The Fuchsia Authors
// Copyright (c) 2013 Travis Geiselbrecht
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <magenta/compiler.h>
#include <debug.h>
#include <kernel/debug.h>
#include <kernel/thread.h>
#include <kernel/timer.h>
#include <kernel/mp.h>

void kernel_init(void);

void kernel_init(void)
{
    // if enabled, configure kernel log
    kernel_log_init();

    // if enabled, configure the kernel's event log
    kernel_evlog_init();

    // initialize the threading system
    dprintf(SPEW, "initializing mp\n");
    mp_init();

    // initialize the threading system
    dprintf(SPEW, "initializing threads\n");
    thread_init();

    // initialize kernel timers
    dprintf(SPEW, "initializing timers\n");
    timer_init();
}

