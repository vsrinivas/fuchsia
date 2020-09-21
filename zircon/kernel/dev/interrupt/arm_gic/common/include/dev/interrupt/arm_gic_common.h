// Copyright 2016 The Fuchsia Authors
// Copyright (c) 2013, Google Inc. All rights reserved.
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#ifndef ZIRCON_KERNEL_DEV_INTERRUPT_ARM_GIC_COMMON_INCLUDE_DEV_INTERRUPT_ARM_GIC_COMMON_H_
#define ZIRCON_KERNEL_DEV_INTERRUPT_ARM_GIC_COMMON_INCLUDE_DEV_INTERRUPT_ARM_GIC_COMMON_H_

#include <sys/types.h>

#include <dev/interrupt.h>

#define GIC_BASE_SGI 0
#define GIC_BASE_PPI 16
#define GIC_BASE_SPI 32

// GIC Revision
enum {
  GICV2 = 2,
  GICV3 = 3,
  GICV4 = 4,
};

enum {
  // Ignore cpu_mask and forward interrupt to all CPUs other than the current cpu
  ARM_GIC_SGI_FLAG_TARGET_FILTER_NOT_SENDER = 0x1,
  // Ignore cpu_mask and forward interrupt to current CPU only
  ARM_GIC_SGI_FLAG_TARGET_FILTER_SENDER = 0x2,
  ARM_GIC_SGI_FLAG_TARGET_FILTER_MASK = 0x3,

  // Only forward the interrupt to CPUs that has the interrupt configured as group 1 (non-secure)
  ARM_GIC_SGI_FLAG_NS = 0x4,
};

// Registers a software generated interrupt handler.
static inline zx_status_t gic_register_sgi_handler(unsigned int vector, int_handler handler) {
  DEBUG_ASSERT(vector < GIC_BASE_PPI);
  return register_permanent_int_handler(vector, handler, nullptr);
}

#endif  // ZIRCON_KERNEL_DEV_INTERRUPT_ARM_GIC_COMMON_INCLUDE_DEV_INTERRUPT_ARM_GIC_COMMON_H_
