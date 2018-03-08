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

bool arm_gic_is_registered(void) {
    return gic_ops == nullptr ? false : true;
}

/* Returns the GICH_HCR value */
uint32_t gic_read_gich_hcr(void) {
    return gic_ops->read_gich_hcr();
}

/* Writes to the GICH_HCR register */
void gic_write_gich_hcr(uint32_t val) {
    return gic_ops->write_gich_hcr(val);
}

/* Returns the GICH_VTR value */
uint32_t gic_read_gich_vtr(void) {
    return gic_ops->read_gich_vtr();
}

/* Writes to the GICH_VTR register */
void gic_write_gich_vtr(uint32_t val) {
    return gic_ops->write_gich_vtr(val);
}

/* Returns the GICH_VMCR value */
uint32_t gic_read_gich_vmcr(void) {
    return gic_ops->read_gich_vmcr();
}

/* Writes to the GICH_VMCR register */
void gic_write_gich_vmcr(uint32_t val) {
    return gic_ops->write_gich_vmcr(val);
}

/* Returns the GICH_ELRS value */
uint64_t gic_read_gich_elrs(void) {
    return gic_ops->read_gich_elrs();
}

/* Writes to the GICH_ELRS register */
void gic_write_gich_elrs(uint64_t val) {
    return gic_ops->write_gich_elrs(val);
}

/* Returns the GICH_LRn value */
uint64_t gic_read_gich_lr(uint32_t idx) {
    return gic_ops->read_gich_lr(idx);
}

/* Writes to the GICH_LR register */
void gic_write_gich_lr(uint32_t idx, uint64_t val) {
    return gic_ops->write_gich_lr(idx, val);
}

/* Get the GICV physical address */
zx_status_t gic_get_gicv(paddr_t* gicv_paddr) {
    return gic_ops->get_gicv(gicv_paddr);
}

uint64_t gic_set_vector(uint32_t vector) {
    return gic_ops->set_vector(vector);
}

uint32_t gic_get_vector(uint32_t i) {
    return gic_ops->get_vector(i);
}

uint32_t gic_get_num_lrs() {
    return gic_ops->get_num_lrs();
}