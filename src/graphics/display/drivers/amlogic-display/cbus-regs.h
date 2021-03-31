// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_GRAPHICS_DISPLAY_DRIVERS_AMLOGIC_DISPLAY_CBUS_REGS_H_
#define SRC_GRAPHICS_DISPLAY_DRIVERS_AMLOGIC_DISPLAY_CBUS_REGS_H_

#define READ32_CBUS_REG(a) cbus_mmio_->Read32(0x400 + a)
#define WRITE32_CBUS_REG(a, v) cbus_mmio_->Write32(v, 0x400 + a)

// Offsets
#define PAD_PULL_UP_EN_REG3 (0x4b << 2)
#define PAD_PULL_UP_REG3 (0x3d << 2)
#define P_PREG_PAD_GPIO3_EN_N (0x19 << 2)
#define PERIPHS_PIN_MUX_B (0xbb << 2)

#endif  // SRC_GRAPHICS_DISPLAY_DRIVERS_AMLOGIC_DISPLAY_CBUS_REGS_H_
