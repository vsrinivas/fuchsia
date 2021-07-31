// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#ifndef SRC_CONNECTIVITY_BLUETOOTH_HCI_EMULATOR_CONTROLLER_H_
#define SRC_CONNECTIVITY_BLUETOOTH_HCI_EMULATOR_CONTROLLER_H_

#include <fuchsia/hardware/bluetooth/llcpp/fidl.h>
#include <lib/ddk/driver.h>

#include <ddktl/device.h>
#include <fbl/string_buffer.h>

#include "src/connectivity/bluetooth/hci/emulator/device.h"
#include "src/connectivity/bluetooth/hci/emulator/log.h"

namespace bt_hci_emulator {

class EmulatorController;
using EmulatorControllerDeviceType =
    ddk::Device<EmulatorController,
                ddk::Messageable<fuchsia_hardware_bluetooth::EmulatorController>::Mixin>;

class EmulatorController : public EmulatorControllerDeviceType {
 public:
  explicit EmulatorController(zx_device_t* parent) : EmulatorControllerDeviceType(parent) {}

  zx_status_t Bind() {
    return DdkAdd(ddk::DeviceAddArgs("bt_hci_emulator").set_flags(DEVICE_ADD_NON_BINDABLE));
  }

  // Device Protocol
  void DdkRelease() { delete this; }

 private:
  // FIDL Interface EmulatorController.
  void Create(CreateRequestView request, CreateCompleter::Sync& completer) override {
    fbl::StringBuffer<zx_MAX_NAME_LEN> name;
    name.AppendPrintf("emulator-%u", num_devices_++);
    auto dev = std::make_unique<bt_hci_emulator::Device>(zxdev());
    zx_status_t status = dev->Bind(std::string_view(name));
    if (status != ZX_OK) {
      logf(ERROR, "failed to bind: %s\n", zx_status_get_string(status));
      completer.ReplyError(status);
    } else {
      // The driver runtime has taken ownership of |dev|.
      dev.release();
      completer.ReplySuccess(fidl::StringView::FromExternal(name.data(), name.size()));
    }
  }

  uint32_t num_devices_ = 0;
};

}  // namespace bt_hci_emulator

#endif  // SRC_CONNECTIVITY_BLUETOOTH_HCI_EMULATOR_CONTROLLER_H_
