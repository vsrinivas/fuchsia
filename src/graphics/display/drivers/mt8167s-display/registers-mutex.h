// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_GRAPHICS_DISPLAY_DRIVERS_MT8167S_DISPLAY_REGISTERS_MUTEX_H_
#define SRC_GRAPHICS_DISPLAY_DRIVERS_MT8167S_DISPLAY_REGISTERS_MUTEX_H_

#include <hwreg/bitfields.h>
#include <hwreg/mmio.h>

//////////////////////////////////////////////////
// MIPI TX Registers
//////////////////////////////////////////////////
#define MUTEX_INTEN (0x0000)
#define MUTEX_INTSTA (0x0004)
#define MUTEX0_EN (0x0020)
#define MUTEX0_RST (0x0028)
#define MUTEX0_MOD (0x002C)
#define MUTEX0_SOF (0x0030)

namespace mt8167s_display {

class MutexIntenReg : public hwreg::RegisterBase<MutexIntenReg, uint32_t> {
 public:
  static auto Get() { return hwreg::RegisterAddr<MutexIntenReg>(MUTEX_INTEN); }
};

class MutexIntstaReg : public hwreg::RegisterBase<MutexIntstaReg, uint32_t> {
 public:
  static auto Get() { return hwreg::RegisterAddr<MutexIntstaReg>(MUTEX_INTSTA); }
};

class Mutex0EnReg : public hwreg::RegisterBase<Mutex0EnReg, uint32_t> {
 public:
  DEF_BIT(0, enable);
  static auto Get() { return hwreg::RegisterAddr<Mutex0EnReg>(MUTEX0_EN); }
};

class Mutex0RstReg : public hwreg::RegisterBase<Mutex0RstReg, uint32_t> {
 public:
  DEF_BIT(0, reset);
  static auto Get() { return hwreg::RegisterAddr<Mutex0RstReg>(MUTEX0_RST); }
};

class Mutex0ModReg : public hwreg::RegisterBase<Mutex0ModReg, uint32_t> {
 public:
  DEF_FIELD(17, 0, mod);
  static auto Get() { return hwreg::RegisterAddr<Mutex0ModReg>(MUTEX0_MOD); }
};

class Mutex0SofReg : public hwreg::RegisterBase<Mutex0SofReg, uint32_t> {
 public:
  DEF_FIELD(2, 0, sof);
  static auto Get() { return hwreg::RegisterAddr<Mutex0SofReg>(MUTEX0_SOF); }
};

}  // namespace mt8167s_display

#endif  // SRC_GRAPHICS_DISPLAY_DRIVERS_MT8167S_DISPLAY_REGISTERS_MUTEX_H_
