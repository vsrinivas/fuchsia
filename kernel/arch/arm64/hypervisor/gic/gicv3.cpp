// Copyright 2018 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <arch/arm64/hypervisor/gic/el2_gic.h>
#include <arch/arm64/hypervisor/gic/gicv3.h>
#include <dev/interrupt/arm_gic_hw_interface.h>
#include <dev/interrupt/arm_gicv3_regs.h>

/* Returns the GICH_HCR value */
static uint32_t gicv3_read_gich_hcr(void) {
    return arm64_el2_gicv3_read_gich_hcr();
}

/* Writes to the GICH_HCR register */
static void gicv3_write_gich_hcr(uint32_t val) {
    arm64_el2_gicv3_write_gich_hcr(val);
}

/* Returns the GICH_VTR value */
static uint32_t gicv3_read_gich_vtr(void) {
    return arm64_el2_gicv3_read_gich_vtr();
}

/* Writes to the GICH_VTR register */
static void gicv3_write_gich_vtr(uint32_t val) {
    arm64_el2_gicv3_write_gich_vtr(val);
}

/* Returns the GICH_VMCR value */
static uint32_t gicv3_read_gich_vmcr(void) {
    return arm64_el2_gicv3_read_gich_vmcr();
}

/* Writes to the GICH_VMCR register */
static void gicv3_write_gich_vmcr(uint32_t val) {
    arm64_el2_gicv3_write_gich_vmcr(val);
}

/* Returns the GICH_ELRS value */
static uint64_t gicv3_read_gich_elrs(void) {
    return arm64_el2_gicv3_read_gich_elrs();
}

/* Writes to the GICH_ELRS register */
static void gicv3_write_gich_elrs(uint64_t val) {
}

/* Returns the GICH_LRn value */
static uint64_t gicv3_read_gich_lr(uint32_t idx) {
    return arm64_el2_gicv3_read_gich_lr(idx);
}

/* Writes to the GICH_LR register */
static void gicv3_write_gich_lr(uint32_t idx, uint64_t val) {
    arm64_el2_gicv3_write_gich_lr(val, idx);
}

static zx_status_t gicv3_get_gicv(paddr_t* gicv_paddr) {
    // Check for presence of GICv3 virtualisation extensions.
    // We return ZX_ERR_NOT_FOUND since this API is used to get
    // address of GICV base to map it to guest
    // On GICv3 we do not need to map this region, since we use system registers

    return ZX_ERR_NOT_FOUND;
}

static uint64_t gicv3_set_vector(uint32_t vector) {
    return (vector & ICH_LR_VIRTUAL_ID_MASK) | ICH_LR_GROUP1 | ICH_LR_PENDING;
}

static uint32_t gicv3_get_vector(uint32_t i) {
    return gicv3_read_gich_lr(i) & ICH_LR_VIRTUAL_ID_MASK;
}

static uint32_t gicv3_get_num_lrs() {
    return (gicv3_read_gich_vtr() & ICH_VTR_LIST_REGS_MASK) + 1;
}

static const struct arm_gic_hw_interface_ops gic_hw_register_ops = {
    .read_gich_hcr = gicv3_read_gich_hcr,
    .write_gich_hcr = gicv3_write_gich_hcr,
    .read_gich_vtr = gicv3_read_gich_vtr,
    .write_gich_vtr = gicv3_write_gich_vtr,
    .read_gich_vmcr = gicv3_read_gich_vmcr,
    .write_gich_vmcr = gicv3_write_gich_vmcr,
    .read_gich_elrs = gicv3_read_gich_elrs,
    .write_gich_elrs = gicv3_write_gich_elrs,
    .read_gich_lr = gicv3_read_gich_lr,
    .write_gich_lr = gicv3_write_gich_lr,
    .get_gicv = gicv3_get_gicv,
    .set_vector = gicv3_set_vector,
    .get_vector = gicv3_get_vector,
    .get_num_lrs = gicv3_get_num_lrs,
};

void gicv3_hw_interface_register(void) {
    arm_gic_hw_interface_register(&gic_hw_register_ops);
}

bool gicv3_is_gic_registered(void) {
    return arm_gic_is_registered();
}