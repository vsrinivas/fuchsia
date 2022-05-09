// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_INPUT_DRIVERS_PC_PS2_REGISTERS_H_
#define SRC_UI_INPUT_DRIVERS_PC_PS2_REGISTERS_H_

#include <hwreg/bitfields.h>

namespace i8042 {

class StatusReg : public hwreg::RegisterBase<StatusReg, uint8_t> {
 public:
  DEF_BIT(0, obf);
  DEF_BIT(1, ibf);
  DEF_BIT(2, muxerr);
  DEF_BIT(3, cmddat);
  DEF_BIT(4, keylock);
  DEF_BIT(5, auxdata);
  DEF_BIT(6, timeout);
  DEF_BIT(7, parity);
};

class ControlReg : public hwreg::RegisterBase<ControlReg, uint8_t> {
 public:
  DEF_BIT(0, kbdint);
  DEF_BIT(1, auxint);
  DEF_BIT(2, sys_flag);
  DEF_BIT(3, ignkeylk);
  DEF_BIT(4, kbddis);
  DEF_BIT(5, auxdis);
  DEF_BIT(6, xlate);
};

}  // namespace i8042

#endif  // SRC_UI_INPUT_DRIVERS_PC_PS2_REGISTERS_H_
