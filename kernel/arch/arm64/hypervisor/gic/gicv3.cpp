// Copyright 2018 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <arch/arm64/hypervisor/gic/el2.h>
#include <arch/arm64/hypervisor/gic/gicv3.h>
#include <arch/ops.h>
#include <dev/interrupt/arm_gic_hw_interface.h>
#include <dev/interrupt/arm_gicv3_regs.h>

static constexpr uint8_t kNumGroups = 2;
static constexpr uint32_t kNumAprs = 4;
static constexpr uint32_t kNumLrs = 16;

static void gicv3_write_gich_hcr(uint32_t val) {
    arm64_el2_gicv3_write_gich_hcr(val);
}

static uint32_t gicv3_read_gich_vtr() {
    return arm64_el2_gicv3_read_gich_vtr();
}

static uint32_t gicv3_default_gich_vmcr() {
    // From ARM GIC v3/v4, Section 8.4.8: VFIQEn - In implementations where the
    // Non-secure copy of ICC_SRE_EL1.SRE is always 1, this bit is RES 1.
    return ICH_VMCR_VPMR | ICH_VMCR_VFIQEN | ICH_VMCR_VENG1;
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

static uint32_t gicv3_read_gich_apr(uint8_t grp, uint32_t idx) {
    DEBUG_ASSERT(grp < kNumGroups);
    DEBUG_ASSERT(idx < kNumAprs);
    return arm64_el2_gicv3_read_gich_apr(idx, grp);
}

static void gicv3_write_gich_apr(uint8_t grp, uint32_t idx, uint32_t val) {
    DEBUG_ASSERT(grp < kNumGroups);
    DEBUG_ASSERT(idx < kNumAprs);
    arm64_el2_gicv3_write_gich_apr(val, idx, grp);
}

static uint64_t gicv3_read_gich_lr(uint32_t idx) {
    DEBUG_ASSERT(idx < kNumLrs);
    return arm64_el2_gicv3_read_gich_lr(idx);
}

static void gicv3_write_gich_lr(uint32_t idx, uint64_t val) {
    DEBUG_ASSERT(idx < kNumLrs);
    if (val & ICH_LR_HARDWARE) {
        // We are adding a physical interrupt to a list register, therefore we
        // mark the physical interrupt as active on the physical distributor so
        // that the guest can deactivate it directly.
        uint32_t vector = ICH_LR_VIRTUAL_ID(val);
        uint32_t reg = vector / 32;
        uint32_t mask = 1u << (vector % 32);
        // Since we use affinity routing, if this vector is associated with an
        // SGI or PPI, we should talk to the redistributor for the current CPU.
        if (vector < 32) {
            cpu_num_t cpu_num = arch_curr_cpu_num();
            GICREG(0, GICR_ISACTIVER0(cpu_num)) = mask;
        } else {
            GICREG(0, GICD_ISACTIVER(reg)) = mask;
        }
    }
    arm64_el2_gicv3_write_gich_lr(val, idx);
}

static zx_status_t gicv3_get_gicv(paddr_t* gicv_paddr) {
    // Check for presence of GICv3 virtualisation extensions.
    // We return ZX_ERR_NOT_FOUND since this API is used to get
    // address of GICV base to map it to guest
    // On GICv3 we do not need to map this region, since we use system registers
    return ZX_ERR_NOT_FOUND;
}

static uint64_t gicv3_get_lr_from_vector(bool hw, uint8_t prio, uint32_t vector) {
    uint64_t lr = ICH_LR_PENDING | ICH_LR_GROUP1 | ICH_LR_PRIORITY(prio) |
        ICH_LR_VIRTUAL_ID(vector);
    if (hw) {
        lr |= ICH_LR_HARDWARE | ICH_LR_PHYSICAL_ID(vector);
    }
    return lr;
}

static uint32_t gicv3_get_vector_from_lr(uint64_t lr) {
    return lr & ICH_LR_VIRTUAL_ID(UINT64_MAX);
}

static uint32_t gicv3_get_num_pres() {
    return ICH_VTR_PRES(gicv3_read_gich_vtr());
}

static uint32_t gicv3_get_num_lrs() {
    return ICH_VTR_LRS(gicv3_read_gich_vtr());
}

static const struct arm_gic_hw_interface_ops gic_hw_register_ops = {
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
    .get_num_pres = gicv3_get_num_pres,
    .get_num_lrs = gicv3_get_num_lrs,
};

void gicv3_hw_interface_register() {
    arm_gic_hw_interface_register(&gic_hw_register_ops);
}
