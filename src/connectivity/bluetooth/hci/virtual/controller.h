// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#ifndef SRC_CONNECTIVITY_BLUETOOTH_HCI_VIRTUAL_CONTROLLER_H_
#define SRC_CONNECTIVITY_BLUETOOTH_HCI_VIRTUAL_CONTROLLER_H_

#include <fidl/fuchsia.hardware.bluetooth/cpp/wire.h>
#include <lib/ddk/driver.h>

#include <ddktl/device.h>
#include <fbl/string_buffer.h>

#include "src/connectivity/bluetooth/hci/virtual/emulator.h"
#include "src/connectivity/bluetooth/hci/virtual/log.h"
#include "src/connectivity/bluetooth/hci/virtual/loopback.h"

namespace bt_hci_virtual {

class VirtualController;
using VirtualControllerDeviceType =
    ddk::Device<VirtualController,
                ddk::Messageable<fuchsia_hardware_bluetooth::VirtualController>::Mixin>;

class VirtualController : public VirtualControllerDeviceType {
 public:
  explicit VirtualController(zx_device_t* parent) : VirtualControllerDeviceType(parent) {}

  zx_status_t Bind() {
    return DdkAdd(ddk::DeviceAddArgs("bt_hci_virtual").set_flags(DEVICE_ADD_NON_BINDABLE));
  }

  // Device Protocol
  void DdkRelease() { delete this; }

 private:
  // FIDL Interface VirtualController.
  void CreateEmulator(CreateEmulatorCompleter::Sync& completer) override {
    fbl::StringBuffer<zx_MAX_NAME_LEN> name;
    name.AppendPrintf("emulator-%u", num_devices_++);
    auto dev = std::make_unique<bt_hci_virtual::EmulatorDevice>(zxdev());
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

  void CreateLoopbackDevice(CreateLoopbackDeviceRequestView request,
                            CreateLoopbackDeviceCompleter::Sync& completer) override {
    // chain new looback device off this device.
    auto dev = std::make_unique<bt_hci_virtual::LoopbackDevice>(zxdev());
    auto channel = request->channel.release();
    zx_status_t status = dev->Bind(channel);
    if (status != ZX_OK) {
      logf(ERROR, "failed to bind: %s\n", zx_status_get_string(status));
    } else {
      // The driver runtime has taken ownership of |dev|.
      __UNUSED bt_hci_virtual::LoopbackDevice* unused = dev.release();
    }
  }

  uint32_t num_devices_ = 0;
};

}  // namespace bt_hci_virtual

#endif  // SRC_CONNECTIVITY_BLUETOOTH_HCI_VIRTUAL_CONTROLLER_H_
