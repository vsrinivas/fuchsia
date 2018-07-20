// Copyright 2018 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <dev/interrupt/arm_gic_hw_interface.h>
#include <vm/pmm.h>
#include <arch/arm64/hypervisor/gic/gicv2.h>
#include <dev/interrupt/arm_gicv2_regs.h>

// Representation of GICH registers. For details please refer to ARM Generic Interrupt
// Controller Architecture Specification Version 2, 5.3 GIC virtual interface control
// registers.
typedef struct Gich {
    uint32_t hcr;
    uint32_t vtr;
    uint32_t vmcr;
    uint32_t reserved0;
    uint32_t misr;
    uint32_t reserved1[3];
    uint64_t eisr;
    uint32_t reserved2[2];
    uint64_t elrsr;
    uint32_t reserved3[46];
    uint32_t apr;
    uint32_t reserved4[3];
    uint32_t lr[64];
} __attribute__((__packed__)) Gich;

static_assert(__offsetof(Gich, hcr) == 0x00, "");
static_assert(__offsetof(Gich, vtr) == 0x04, "");
static_assert(__offsetof(Gich, vmcr) == 0x08, "");
static_assert(__offsetof(Gich, misr) == 0x10, "");
static_assert(__offsetof(Gich, eisr) == 0x20, "");
static_assert(__offsetof(Gich, elrsr) == 0x30, "");
static_assert(__offsetof(Gich, apr) == 0xf0, "");
static_assert(__offsetof(Gich, lr) == 0x100, "");

static volatile Gich* gich = NULL;

static uint32_t gicv2_read_gich_hcr() {
    return gich->hcr;
}

static void gicv2_write_gich_hcr(uint32_t val) {
    gich->hcr = val;
}

static uint32_t gicv2_read_gich_vtr() {
    return gich->vtr;
}

static uint32_t gicv2_default_gich_vmcr() {
    return GICH_VMCR_VPMR_MASK | GICH_VMCR_VENG0;
}

static uint32_t gicv2_read_gich_vmcr() {
    return gich->vmcr;
}

static void gicv2_write_gich_vmcr(uint32_t val) {
    gich->vmcr = val;
}

static uint32_t gicv2_read_gich_misr() {
    return gich->misr;
}

static uint64_t gicv2_read_gich_elrsr() {
    return gich->elrsr;
}

static uint32_t gicv2_read_gich_apr() {
    return gich->apr;
}

static void gicv2_write_gich_apr(uint32_t val) {
    gich->apr = val;
}

static uint64_t gicv2_read_gich_lr(uint32_t idx) {
    return gich->lr[idx];
}

static void gicv2_write_gich_lr(uint32_t idx, uint64_t val) {
    gich->lr[idx] = static_cast<uint32_t>(val);
}

static zx_status_t gicv2_get_gicv(paddr_t* gicv_paddr) {
    // Check for presence of GICv2 virtualisation extensions.
    if (GICV_OFFSET == 0)
        return ZX_ERR_NOT_SUPPORTED;
    *gicv_paddr = vaddr_to_paddr(reinterpret_cast<void*>(GICV_ADDRESS));
    return ZX_OK;
}

static uint64_t gicv2_get_lr_from_vector(uint32_t vector) {
    return (vector & GICH_LR_VIRTUAL_ID_MASK) | GICH_LR_PENDING;
}

static uint32_t gicv2_get_vector_from_lr(uint64_t lr) {
    return lr & GICH_LR_VIRTUAL_ID_MASK;
}

static uint32_t gicv2_get_num_lrs() {
    return (gicv2_read_gich_vtr() & GICH_VTR_LIST_REGS_MASK) + 1;
}

static const struct arm_gic_hw_interface_ops gic_hw_register_ops = {
    .read_gich_hcr = gicv2_read_gich_hcr,
    .write_gich_hcr = gicv2_write_gich_hcr,
    .read_gich_vtr = gicv2_read_gich_vtr,
    .default_gich_vmcr = gicv2_default_gich_vmcr,
    .read_gich_vmcr = gicv2_read_gich_vmcr,
    .write_gich_vmcr = gicv2_write_gich_vmcr,
    .read_gich_misr = gicv2_read_gich_misr,
    .read_gich_elrsr = gicv2_read_gich_elrsr,
    .read_gich_apr = gicv2_read_gich_apr,
    .write_gich_apr = gicv2_write_gich_apr,
    .read_gich_lr = gicv2_read_gich_lr,
    .write_gich_lr = gicv2_write_gich_lr,
    .get_gicv = gicv2_get_gicv,
    .get_lr_from_vector = gicv2_get_lr_from_vector,
    .get_vector_from_lr = gicv2_get_vector_from_lr,
    .get_num_lrs = gicv2_get_num_lrs,
};

void gicv2_hw_interface_register() {
    // Populate GICH
    gich = reinterpret_cast<volatile Gich*>(GICH_ADDRESS);
    arm_gic_hw_interface_register(&gic_hw_register_ops);
}
