// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT
#pragma once

#include <zircon/compiler.h>
#include <kernel/mp.h>

__BEGIN_CDECLS

void x86_mmu_mem_type_init(void);
void x86_pat_sync(mp_cpu_mask_t targets);

__END_CDECLS
