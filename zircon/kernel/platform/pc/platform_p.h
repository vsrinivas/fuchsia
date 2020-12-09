// Copyright 2016 The Fuchsia Authors
// Copyright (c) 2009 Corey Tabaka
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#ifndef ZIRCON_KERNEL_PLATFORM_PC_PLATFORM_P_H_
#define ZIRCON_KERNEL_PLATFORM_PC_PLATFORM_P_H_

#include <lib/cbuf.h>
#include <sys/types.h>
#include <zircon/compiler.h>
#include <zircon/types.h>

#include <ktl/span.h>

extern Cbuf console_input_buf;

void pc_init_debug_early(void);
void pc_init_debug(void);
void pc_init_timer_percpu(void);
void pc_mem_init(ktl::span<zbi_mem_range_t> ranges);

void pc_prep_suspend_timer(void);
void pc_resume_timer(void);
void pc_resume_debug(void);
void pc_suspend_debug(void);

#endif  // ZIRCON_KERNEL_PLATFORM_PC_PLATFORM_P_H_
