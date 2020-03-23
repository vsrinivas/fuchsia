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

extern Cbuf console_input_buf;

__BEGIN_CDECLS

/* Do not use anything above this address for PCIe stuff.  A bunch of
 * architectural devices often occupy this area */
#define HIGH_ADDRESS_LIMIT 0xfec00000

extern paddr_t pcie_mem_lo_base;
extern size_t pcie_mem_lo_size;

extern uint16_t pcie_pio_base;
extern uint16_t pcie_pio_size;

void pc_init_debug_early(void);
void pc_init_debug(void);
void pc_init_timer_percpu(void);
void pc_mem_init(void);

void pc_prep_suspend_timer(void);
void pc_resume_timer(void);
void pc_resume_debug(void);
void pc_suspend_debug(void);

typedef void (*enumerate_e820_callback)(uint64_t base, uint64_t size, bool is_mem, void* ctx);
zx_status_t enumerate_e820(enumerate_e820_callback callback, void* ctx);

__END_CDECLS

#endif  // ZIRCON_KERNEL_PLATFORM_PC_PLATFORM_P_H_
