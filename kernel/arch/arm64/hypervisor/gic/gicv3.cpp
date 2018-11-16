// Copyright 2018 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <arch/arm64/hypervisor/gic/el2.h>
#include <arch/arm64/hypervisor/gic/gicv3.h>
#include <dev/interrupt/arm_gic_hw_interface.h>
#include <dev/interrupt/arm_gicv3_regs.h>

static uint32_t gicv3_read_gich_hcr() {
    return arm64_el2_gicv3_read_gich_hcr();
}

static void gicv3_write_gich_hcr(uint32_t val) {
    arm64_el2_gicv3_write_gich_hcr(val);
}

static uint32_t gicv3_read_gich_vtr() {
    return arm64_el2_gicv3_read_gich_vtr();
}

static uint32_t gicv3_default_gich_vmcr() {
    return ICH_VMCR_VPMR_MASK | ICH_VMCR_VENG1;
}

static uint32_t gicv3_read_gich_vmcr() {
    return arm64_el2_gicv3_read_gich_vmcr();
}

static void gicv3_write_gich_vmcr(uint32_t val) {
    arm64_el2_gicv3_write_gich_vmcr(val);
}

static uint32_t gicv3_read_gich_misr() {
    return arm64_el2_gicv3_read_gich_misr();
}

static uint64_t gicv3_read_gich_elrsr() {
    return arm64_el2_gicv3_read_gich_elrsr();
}

static uint32_t gicv3_read_gich_apr() {
    return arm64_el2_gicv3_read_gich_apr();
}

static void gicv3_write_gich_apr(uint32_t val) {
    arm64_el2_gicv3_write_gich_apr(val);
}

static uint64_t gicv3_read_gich_lr(uint32_t idx) {
    return arm64_el2_gicv3_read_gich_lr(idx);
}

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

static uint64_t gicv3_get_lr_from_vector(uint32_t vector) {
    return (vector & ICH_LR_VIRTUAL_ID_MASK) | ICH_LR_GROUP1 | ICH_LR_PENDING;
}

static uint32_t gicv3_get_vector_from_lr(uint64_t lr) {
    return lr & ICH_LR_VIRTUAL_ID_MASK;
}

static uint32_t gicv3_get_num_lrs() {
    return (gicv3_read_gich_vtr() & ICH_VTR_LIST_REGS_MASK) + 1;
}

static const struct arm_gic_hw_interface_ops gic_hw_register_ops = {
    .read_gich_hcr = gicv3_read_gich_hcr,
    .write_gich_hcr = gicv3_write_gich_hcr,
    .read_gich_vtr = gicv3_read_gich_vtr,
    .default_gich_vmcr = gicv3_default_gich_vmcr,
    .read_gich_vmcr = gicv3_read_gich_vmcr,
    .write_gich_vmcr = gicv3_write_gich_vmcr,
    .read_gich_misr = gicv3_read_gich_misr,
    .read_gich_elrsr = gicv3_read_gich_elrsr,
    .read_gich_apr = gicv3_read_gich_apr,
    .write_gich_apr = gicv3_write_gich_apr,
    .read_gich_lr = gicv3_read_gich_lr,
    .write_gich_lr = gicv3_write_gich_lr,
    .get_gicv = gicv3_get_gicv,
    .get_lr_from_vector = gicv3_get_lr_from_vector,
    .get_vector_from_lr = gicv3_get_vector_from_lr,
    .get_num_lrs = gicv3_get_num_lrs,
};

void gicv3_hw_interface_register() {
    arm_gic_hw_interface_register(&gic_hw_register_ops);
}

bool gicv3_is_gic_registered() {
    return arm_gic_is_registered();
}
