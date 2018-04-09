// Copyright 2018 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#pragma once

#include <sys/types.h>
#include <zircon/compiler.h>
#include <zircon/types.h>

__BEGIN_CDECLS

// adds a new peripheral range
zx_status_t add_periph_range(paddr_t base_phys, size_t length);

// called after virtual memory is started to reserve peripheral ranges
// in the kernel's address space
void reserve_periph_ranges(void);

// translates peripheral physical address to virtual address in the big kernel map
vaddr_t periph_paddr_to_vaddr(paddr_t paddr);

__END_CDECLS
