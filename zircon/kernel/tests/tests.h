// Copyright 2016 The Fuchsia Authors
// Copyright (c) 2008-2014 Travis Geiselbrecht
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#pragma once

#include <lib/console.h>
#include <zircon/compiler.h>

__BEGIN_CDECLS

console_cmd uart_tests, thread_tests, sleep_tests, port_tests;
console_cmd clock_tests, timer_diag, timer_stress, benchmarks, fibo;
console_cmd spinner, ref_counted_tests, ref_ptr_tests;
console_cmd unique_ptr_tests, forward_tests, list_tests;
console_cmd hash_tests, vm_tests, auto_call_tests;
console_cmd arena_tests, fifo_tests, alloc_checker_tests;
console_cmd cpu_resume_tests, semaphore_tests;
void unittests(void);

__END_CDECLS
