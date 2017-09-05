// Copyright 2016 The Fuchsia Authors
// Copyright (c) 2008-2014 Travis Geiselbrecht
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#pragma once

#include <lib/console.h>
#include <magenta/compiler.h>

__BEGIN_CDECLS

int thread_tests(void);
int sleep_tests(void);
int port_tests(void);
void printf_tests(void);
void clock_tests(void);
void timer_tests(void);
void benchmarks(void);
int fibo(int argc, const cmd_args* argv);
int spinner(int argc, const cmd_args* argv);
int ref_counted_tests(int argc, const cmd_args* argv);
int ref_ptr_tests(int argc, const cmd_args* argv);
int unique_ptr_tests(int argc, const cmd_args* argv);
int forward_tests(int argc, const cmd_args* argv);
int list_tests(int argc, const cmd_args* argv);
int hash_tests(int argc, const cmd_args* argv);
int vm_tests(int argc, const cmd_args* argv);
int auto_call_tests(int argc, const cmd_args* argv);
int sync_ipi_tests(int argc, const cmd_args* argv);
int arena_tests(int argc, const cmd_args* argv);
int fifo_tests(int argc, const cmd_args* argv);
int alloc_checker_tests(int argc, const cmd_args* argv);
void unittests(void);

__END_CDECLS
