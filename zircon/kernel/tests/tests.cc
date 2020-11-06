// Copyright 2016 The Fuchsia Authors
// Copyright (c) 2008 Travis Geiselbrecht
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include "tests.h"

#include <assert.h>
#include <debug.h>
#include <lib/console.h>
#include <lib/unittest/unittest.h>
#include <platform.h>
#include <zircon/compiler.h>

STATIC_COMMAND_START
STATIC_COMMAND("thread_tests", "test the scheduler", &thread_tests)
STATIC_COMMAND("clock_tests", "test clocks", &clock_tests)
STATIC_COMMAND("sleep_tests", "tests sleep", &sleep_tests)
STATIC_COMMAND("bench", "miscellaneous benchmarks", &benchmarks)
STATIC_COMMAND("fibo", "threaded fibonacci", &fibo)
STATIC_COMMAND("spinner", "create a spinning thread", &spinner)
STATIC_COMMAND("timer_diag", "prints timer diagnostics", &timer_diag)
STATIC_COMMAND("timer_stress", "runs a timer stress test", &timer_stress)
STATIC_COMMAND("uart_tests", "tests uart Tx", &uart_tests)
STATIC_COMMAND_END(tests)
