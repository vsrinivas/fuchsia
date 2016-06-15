// Copyright 2016 The Fuchsia Authors
// Copyright (c) 2009 Corey Tabaka
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#pragma once

#include <lib/cbuf.h>

extern cbuf_t console_input_buf;

/* Do not use anything above this address for PCIe stuff.  A bunch of
 * architectural devices often occupy this area */
#define HIGH_ADDRESS_LIMIT 0xfec00000

extern paddr_t pcie_mem_lo_base;
extern size_t pcie_mem_lo_size;

extern uint16_t pcie_pio_base;
extern uint16_t pcie_pio_size;

void platform_init_debug_early(void);
void platform_init_debug(void);
void platform_init_timer_percpu(void);
void platform_mem_init(void);
