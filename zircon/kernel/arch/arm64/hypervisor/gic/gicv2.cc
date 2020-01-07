// Copyright 2018 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <assert.h>
#include <bits.h>

#include <arch/arm64/hypervisor/el2_state.h>
#include <arch/arm64/hypervisor/gic/gicv2.h>
#include <arch/hypervisor.h>
#include <dev/interrupt/arm_gic_hw_interface.h>
#include <dev/interrupt/arm_gicv2_regs.h>
#include <vm/pmm.h>

static constexpr uint32_t kNumLrs = 64;

// Check that InterruptState corresponds exactly to the LR state bits.
static_assert(static_cast<InterruptState>(BITS_SHIFT(0, GICH_LR_ACTIVE_BIT, GICH_LR_PENDING_BIT)) ==
              InterruptState::INACTIVE);
static_assert(static_cast<InterruptState>(BITS_SHIFT(1 << GICH_LR_PENDING_BIT, GICH_LR_ACTIVE_BIT,
                                                     GICH_LR_PENDING_BIT)) ==
              InterruptState::PENDING);
static_assert(static_cast<InterruptState>(BITS_SHIFT(1 << GICH_LR_ACTIVE_BIT, GICH_LR_ACTIVE_BIT,
                                                     GICH_LR_PENDING_BIT)) ==
              InterruptState::ACTIVE);
static_assert(static_cast<InterruptState>(BITS_SHIFT(1 << GICH_LR_PENDING_BIT |
                                                         1 << GICH_LR_ACTIVE_BIT,
                                                     GICH_LR_ACTIVE_BIT, GICH_LR_PENDING_BIT)) ==
              InterruptState::PENDING_AND_ACTIVE);

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
  uint32_t eisr0;
  uint32_t eisr1;
  uint32_t reserved2[2];
  uint32_t elrsr0;
  uint32_t elrsr1;
  uint32_t reserved3[46];
  uint32_t apr;
  uint32_t reserved4[3];
  uint32_t lr[kNumLrs];
} __attribute__((__packed__)) Gich;

static_assert(__offsetof(Gich, hcr) == 0x00, "");
static_assert(__offsetof(Gich, vtr) == 0x04, "");
static_assert(__offsetof(Gich, vmcr) == 0x08, "");
static_assert(__offsetof(Gich, misr) == 0x10, "");
static_assert(__offsetof(Gich, eisr0) == 0x20, "");
static_assert(__offsetof(Gich, eisr1) == 0x24, "");
static_assert(__offsetof(Gich, elrsr0) == 0x30, "");
static_assert(__offsetof(Gich, elrsr1) == 0x34, "");
static_assert(__offsetof(Gich, apr) == 0xf0, "");
static_assert(__offsetof(Gich, lr) == 0x100, "");

static volatile Gich* gich = NULL;

static zx_status_t gicv2_get_gicv(paddr_t* gicv_paddr) {
  // Check for presence of GICv2 virtualisation extensions.
  if (GICV_OFFSET == 0) {
    return ZX_ERR_NOT_SUPPORTED;
  }
  *gicv_paddr = vaddr_to_paddr(reinterpret_cast<void*>(GICV_ADDRESS));
  return ZX_OK;
}

static void gicv2_read_gich_state(IchState* state) {
  DEBUG_ASSERT(state->num_aprs == 1);
  DEBUG_ASSERT(state->num_lrs <= kNumLrs);
  gich->hcr = 0;
  state->vmcr = gich->vmcr;
  state->misr = gich->misr;
  state->elrsr = gich->elrsr0 | (static_cast<uint64_t>(gich->elrsr1) << 32);
  state->apr[0][0] = gich->apr;
  for (uint8_t i = 0; i < state->num_lrs; i++) {
    state->lr[i] = gich->lr[i];
  }
}

static void gicv2_write_gich_state(IchState* state, uint32_t hcr) {
  DEBUG_ASSERT(state->num_aprs == 1);
  DEBUG_ASSERT(state->num_lrs <= kNumLrs);
  gich->hcr = hcr;
  gich->vmcr = state->vmcr;
  gich->apr = static_cast<uint32_t>(state->apr[0][0]);
  for (uint8_t i = 0; i < state->num_lrs; i++) {
    uint32_t lr = static_cast<uint32_t>(state->lr[i]);
    uint32_t vector = GICH_LR_VIRTUAL_ID(lr);
    if ((lr & GICH_LR_HARDWARE) && vector == kTimerVector) {
      // We are translating the physical timer interrupt to the virtual
      // timer interrupt, therefore we are marking the virtual timer interrupt
      // as active on the GIC distributor for the guest to deactivate.
      uint32_t reg = vector / 32;
      uint32_t mask = 1u << (vector % 32);
      GICREG(0, GICD_ISACTIVER(reg)) = mask;
    }
    gich->lr[i] = lr;
  }
}

static uint32_t gicv2_default_gich_vmcr() { return GICH_VMCR_VPMR | GICH_VMCR_VENG0; }

static uint64_t gicv2_get_lr_from_vector(bool hw, uint8_t prio, InterruptState state,
                                         uint32_t vector) {
  uint64_t lr = (static_cast<uint64_t>(state) << GICH_LR_PENDING_BIT) | GICH_LR_PRIORITY(prio) |
                GICH_LR_VIRTUAL_ID(vector);
  if (hw) {
    lr |= GICH_LR_HARDWARE | GICH_LR_PHYSICAL_ID(vector);
  }
  return lr;
}

static uint32_t gicv2_get_vector_from_lr(uint64_t lr, InterruptState* state) {
  *state = static_cast<InterruptState>(BITS_SHIFT(lr, GICH_LR_ACTIVE_BIT, GICH_LR_PENDING_BIT));
  return lr & GICH_LR_VIRTUAL_ID(UINT64_MAX);
}

static uint8_t gicv2_get_num_pres() { return static_cast<uint8_t>(GICH_VTR_PRES(gich->vtr)); }

static uint8_t gicv2_get_num_lrs() { return static_cast<uint8_t>(GICH_VTR_LRS(gich->vtr)); }

static const struct arm_gic_hw_interface_ops gic_hw_register_ops = {
    .get_gicv = gicv2_get_gicv,
    .read_gich_state = gicv2_read_gich_state,
    .write_gich_state = gicv2_write_gich_state,
    .default_gich_vmcr = gicv2_default_gich_vmcr,
    .get_lr_from_vector = gicv2_get_lr_from_vector,
    .get_vector_from_lr = gicv2_get_vector_from_lr,
    .get_num_pres = gicv2_get_num_pres,
    .get_num_lrs = gicv2_get_num_lrs,
};

void gicv2_hw_interface_register() {
  // Populate GICH
  gich = reinterpret_cast<volatile Gich*>(GICH_ADDRESS);
  arm_gic_hw_interface_register(&gic_hw_register_ops);
}
