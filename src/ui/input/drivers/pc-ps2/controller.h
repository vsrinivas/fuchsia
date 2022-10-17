// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_INPUT_DRIVERS_PC_PS2_CONTROLLER_H_
#define SRC_UI_INPUT_DRIVERS_PC_PS2_CONTROLLER_H_

#include <lib/ddk/hw/inout.h>

#include <ddktl/device.h>

#include "src/ui/input/drivers/pc-ps2/commands.h"
#include "src/ui/input/drivers/pc-ps2/registers.h"

namespace i8042 {

constexpr uint16_t kCommandReg = 0x64;
constexpr uint16_t kStatusReg = 0x64;
constexpr uint16_t kDataReg = 0x60;

enum Port {
  kPort1 = 0,
  kPort2 = 1,
};

class Controller;
using ControllerDeviceType = ddk::Device<Controller, ddk::Initializable>;
class Controller : public ControllerDeviceType {
 public:
  explicit Controller(zx_device_t* parent) : ControllerDeviceType(parent) {}
  static zx_status_t Bind(void* ctx, zx_device_t* parent);

  void DdkRelease() {
    init_thread_.join();
    delete this;
  }
  void DdkInit(ddk::InitTxn txn);

  // Send a command to the controller.
  zx::result<std::vector<uint8_t>> SendControllerCommand(Command command,
                                                         cpp20::span<const uint8_t> data);

  // Send a command to the given port.
  zx::result<std::vector<uint8_t>> SendDeviceCommand(Command command, Port port);

  StatusReg ReadStatus();

  uint8_t ReadData();

  // For unit tests
  sync_completion_t& added_children() { return added_children_; }

 private:
  std::thread init_thread_;
  bool has_port2_ = false;
  sync_completion_t added_children_;

  void Flush();

  // Wait for the data register to be ready to write to.
  // Returns true if it's OK to write.
  bool WaitWrite();

  // Wait for the data register to be ready to read from.
  // Returns true if it's OK to read.
  bool WaitRead();
};

}  // namespace i8042

#endif  // SRC_UI_INPUT_DRIVERS_PC_PS2_CONTROLLER_H_
