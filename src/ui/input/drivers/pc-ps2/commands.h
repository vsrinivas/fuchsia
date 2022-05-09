// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_INPUT_DRIVERS_PC_PS2_COMMANDS_H_
#define SRC_UI_INPUT_DRIVERS_PC_PS2_COMMANDS_H_

#include <stdint.h>

namespace i8042 {

struct Command {
  // Command code.
  const uint8_t cmd = 0;
  // Number of bytes of parameter.
  const uint8_t param_count = 0;
  // Number of bytes of response.
  const uint8_t response_count = 0;
};

#define CMD(name, number, params, response)                                \
  inline constexpr Command name = Command {                                \
    .cmd = (number), .param_count = (params), .response_count = (response) \
  }

// Controller commands. Sent via the command register.
CMD(kCmdReadCtl, 0x20, 0, 1);
CMD(kCmdWriteCtl, 0x60, 1, 0);
CMD(kCmdSelfTest, 0xaa, 0, 1);
CMD(kCmdWriteAux, 0xd4, 1, 0);
// First port (typically keyboard)
CMD(kCmdPort1Disable, 0xad, 0, 0);
CMD(kCmdPort1Enable, 0xae, 0, 0);
CMD(kCmdPort1Test, 0xab, 0, 1);
// Second port (typically mouse)
CMD(kCmdPort2Disable, 0xa7, 0, 0);
CMD(kCmdPort2Enable, 0xa8, 0, 0);
CMD(kCmdPort2Test, 0xa9, 0, 1);

// Device commands. Sent to the device via the data register.
CMD(kCmdDeviceIdentify, 0xf2, 0, 3);
CMD(kCmdDeviceScanDisable, 0xf5, 0, 1);
CMD(kCmdDeviceScanEnable, 0xf4, 0, 1);

}  // namespace i8042

#endif  // SRC_UI_INPUT_DRIVERS_PC_PS2_COMMANDS_H_
