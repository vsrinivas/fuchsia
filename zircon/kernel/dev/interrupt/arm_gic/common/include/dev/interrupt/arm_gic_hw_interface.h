// Copyright 2016 The Fuchsia Authors
// Copyright (c) 2013, Google Inc. All rights reserved.
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#ifndef ZIRCON_KERNEL_DEV_INTERRUPT_ARM_GIC_COMMON_INCLUDE_DEV_INTERRUPT_ARM_GIC_HW_INTERFACE_H_
#define ZIRCON_KERNEL_DEV_INTERRUPT_ARM_GIC_COMMON_INCLUDE_DEV_INTERRUPT_ARM_GIC_HW_INTERFACE_H_

#include <sys/types.h>

enum class InterruptState : uint8_t {
  INACTIVE = 0,
  PENDING = 1,
  ACTIVE = 2,
  PENDING_AND_ACTIVE = 3,
};

struct IchState;

// GIC HW interface
struct arm_gic_hw_interface_ops {
  zx_status_t (*get_gicv)(paddr_t* gicv_paddr);
  void (*read_gich_state)(IchState* state);
  void (*write_gich_state)(IchState* state, uint32_t hcr);
  uint32_t (*default_gich_vmcr)();
  uint64_t (*get_lr_from_vector)(bool hw, uint8_t prio, InterruptState state, uint32_t vector);
  uint32_t (*get_vector_from_lr)(uint64_t lr, InterruptState* state);
  uint8_t (*get_num_pres)();
  uint8_t (*get_num_lrs)();
};

// Get the GICV physical address.
zx_status_t gic_get_gicv(paddr_t* gicv_paddr);

// Reads the GICH state.
void gic_read_gich_state(IchState* state);

// Writes the GICH state.
void gic_write_gich_state(IchState* state, uint32_t hcr);

// Returns the default GICH_VMCR value. Used to initialize GICH_VMCR.
uint32_t gic_default_gich_vmcr();

// Returns a list register based on the given interrupt vector.
uint64_t gic_get_lr_from_vector(bool hw, uint8_t prio, InterruptState state, uint32_t vector);

// Returns an interrupt vector based on the given list register.
uint32_t gic_get_vector_from_lr(uint64_t lr, InterruptState* state);

// Returns the number of preemption bits.
uint8_t gic_get_num_pres();

// Returns the number of list registers.
uint8_t gic_get_num_lrs();

// Registers the ops of the GIC driver initialized with HW interface layer.
void arm_gic_hw_interface_register(const struct arm_gic_hw_interface_ops* ops);

// Returns whether the GIC driver has been registered.
bool arm_gic_is_registered();

#endif  // ZIRCON_KERNEL_DEV_INTERRUPT_ARM_GIC_COMMON_INCLUDE_DEV_INTERRUPT_ARM_GIC_HW_INTERFACE_H_
