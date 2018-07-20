// Copyright 2016 The Fuchsia Authors
// Copyright (c) 2012-2015 Travis Geiselbrecht
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <assert.h>
#include <dev/interrupt/arm_gic_common.h>
#include <dev/interrupt/arm_gic_hw_interface.h>
#include <err.h>
#include <zircon/types.h>

static const struct arm_gic_hw_interface_ops* gic_ops = nullptr;

/* Registers the ops of the GIC driver initialized with HW interface layer */
void arm_gic_hw_interface_register(const struct arm_gic_hw_interface_ops* ops) {
    DEBUG_ASSERT(ops != nullptr);
    gic_ops = ops;
}

bool arm_gic_is_registered() {
    return gic_ops == nullptr ? false : true;
}

uint32_t gic_read_gich_hcr() {
    return gic_ops->read_gich_hcr();
}

void gic_write_gich_hcr(uint32_t val) {
    return gic_ops->write_gich_hcr(val);
}

uint32_t gic_read_gich_vtr() {
    return gic_ops->read_gich_vtr();
}

uint32_t gic_default_gich_vmcr() {
    return gic_ops->default_gich_vmcr();
}

uint32_t gic_read_gich_vmcr() {
    return gic_ops->read_gich_vmcr();
}

void gic_write_gich_vmcr(uint32_t val) {
    return gic_ops->write_gich_vmcr(val);
}

uint64_t gic_read_gich_elrsr() {
    return gic_ops->read_gich_elrsr();
}

uint32_t gic_read_gich_misr() {
    return gic_ops->read_gich_misr();
}

uint64_t gic_read_gich_lr(uint32_t idx) {
    return gic_ops->read_gich_lr(idx);
}

void gic_write_gich_lr(uint32_t idx, uint64_t val) {
    return gic_ops->write_gich_lr(idx, val);
}

zx_status_t gic_get_gicv(paddr_t* gicv_paddr) {
    return gic_ops->get_gicv(gicv_paddr);
}

uint64_t gic_get_lr_from_vector(uint32_t vector) {
    return gic_ops->get_lr_from_vector(vector);
}

uint32_t gic_get_vector_from_lr(uint64_t lr) {
    return gic_ops->get_vector_from_lr(lr);
}

uint32_t gic_get_num_lrs() {
    return gic_ops->get_num_lrs();
}

void gic_write_gich_apr(uint32_t val) {
    return gic_ops->write_gich_apr(val);
}

uint32_t gic_read_gich_apr() {
    return gic_ops->read_gich_apr();
}
