// Copyright 2017 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#pragma once

#include <zircon/compiler.h>
#include <arch/defines.h>
#include <zircon/types.h>

__BEGIN_CDECLS

extern uint32_t arm64_el2_gicv3_read_gich_hcr();
extern void arm64_el2_gicv3_write_gich_hcr(uint32_t val);
extern uint32_t arm64_el2_gicv3_read_gich_vtr();
extern void arm64_el2_gicv3_write_gich_vtr(uint32_t val);
extern uint32_t arm64_el2_gicv3_read_gich_vmcr();
extern void arm64_el2_gicv3_write_gich_vmcr(uint32_t val);
extern uint32_t arm64_el2_gicv3_read_gich_elrs();
extern uint64_t arm64_el2_gicv3_read_gich_lr(uint32_t index);
extern void arm64_el2_gicv3_write_gich_lr(uint64_t val, uint32_t index);

__END_CDECLS