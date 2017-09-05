// Copyright 2016 The Fuchsia Authors
// Copyright (c) 2008 Travis Geiselbrecht
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include "tests.h"

#include <debug.h>
#include <magenta/compiler.h>

#if defined(WITH_LIB_CONSOLE)
#include <lib/console.h>

#include <assert.h>
#include <err.h>
#include <platform.h>
#include <unittest.h>

STATIC_COMMAND_START
STATIC_COMMAND("printf_tests", "test printf", (console_cmd)&printf_tests)
STATIC_COMMAND("thread_tests", "test the scheduler", (console_cmd)&thread_tests)
STATIC_COMMAND("clock_tests", "test clocks", (console_cmd)&clock_tests)
STATIC_COMMAND("sleep_tests", "tests sleep", (console_cmd)&sleep_tests)
STATIC_COMMAND("bench", "miscellaneous benchmarks", (console_cmd)&benchmarks)
STATIC_COMMAND("fibo", "threaded fibonacci", (console_cmd)&fibo)
STATIC_COMMAND("spinner", "create a spinning thread", (console_cmd)&spinner)
STATIC_COMMAND("sync_ipi_tests", "test synchronous IPIs", (console_cmd)&sync_ipi_tests)
STATIC_COMMAND("timer_tests", "tests timers", (console_cmd)&timer_tests)
STATIC_COMMAND_END(tests);

#endif
