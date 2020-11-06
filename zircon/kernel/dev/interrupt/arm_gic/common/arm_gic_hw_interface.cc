// Copyright 2016 The Fuchsia Authors
// Copyright (c) 2012-2015 Travis Geiselbrecht
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <assert.h>
#include <zircon/types.h>

#include <dev/interrupt/arm_gic_common.h>
#include <dev/interrupt/arm_gic_hw_interface.h>

static const struct arm_gic_hw_interface_ops* gic_ops = nullptr;

zx_status_t gic_get_gicv(paddr_t* gicv_paddr) { return gic_ops->get_gicv(gicv_paddr); }

void gic_read_gich_state(IchState* state) { gic_ops->read_gich_state(state); }

void gic_write_gich_state(IchState* state, uint32_t hcr) { gic_ops->write_gich_state(state, hcr); }

uint32_t gic_default_gich_vmcr() { return gic_ops->default_gich_vmcr(); }

uint64_t gic_get_lr_from_vector(bool hw, uint8_t prio, InterruptState state, uint32_t vector) {
  return gic_ops->get_lr_from_vector(hw, prio, state, vector);
}

uint32_t gic_get_vector_from_lr(uint64_t lr, InterruptState* state) {
  return gic_ops->get_vector_from_lr(lr, state);
}

uint8_t gic_get_num_pres() { return gic_ops->get_num_pres(); }

uint8_t gic_get_num_lrs() { return gic_ops->get_num_lrs(); }

void arm_gic_hw_interface_register(const struct arm_gic_hw_interface_ops* ops) { gic_ops = ops; }

bool arm_gic_is_registered() { return gic_ops != nullptr; }
