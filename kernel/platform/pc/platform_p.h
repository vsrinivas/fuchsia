// Copyright 2016 The Fuchsia Authors
// Copyright (c) 2009 Corey Tabaka
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#pragma once

#include <sys/types.h>
#include <dev/pcie_platform.h>
#include <lib/cbuf.h>
#include <magenta/compiler.h>

extern cbuf_t console_input_buf;

__BEGIN_CDECLS

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

status_t x86_alloc_msi_block(uint requested_irqs, bool can_target_64bit,
                             bool is_msix, pcie_msi_block_t* out_block);
void x86_free_msi_block(pcie_msi_block_t* block);
void x86_register_msi_handler(const pcie_msi_block_t* block,
                              uint msi_id,
                              int_handler handler,
                              void* ctx);

status_t platform_configure_watchdog(uint32_t frequency);

typedef void (*enumerate_e820_callback)(uint64_t base, uint64_t size, bool is_mem, void* ctx);
status_t enumerate_e820(enumerate_e820_callback callback, void* ctx);

__END_CDECLS
