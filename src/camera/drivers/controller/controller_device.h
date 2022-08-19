// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_CAMERA_DRIVERS_CONTROLLER_CONTROLLER_DEVICE_H_
#define SRC_CAMERA_DRIVERS_CONTROLLER_CONTROLLER_DEVICE_H_

#include <fidl/fuchsia.hardware.camera/cpp/wire.h>
#include <fuchsia/hardware/gdc/cpp/banjo.h>
#include <fuchsia/hardware/ge2d/cpp/banjo.h>
#include <fuchsia/hardware/isp/cpp/banjo.h>
#include <fuchsia/hardware/platform/device/c/banjo.h>
#include <fuchsia/hardware/sysmem/cpp/banjo.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/async/cpp/wait.h>
#include <lib/ddk/platform-defs.h>
#include <lib/device-protocol/pdev.h>
#include <lib/device-protocol/platform-device.h>
#include <lib/fidl-utils/bind.h>
#include <zircon/fidl.h>

#include <ddktl/device.h>
#include <ddktl/protocol/empty-protocol.h>

#include "src/camera/drivers/controller/controller_protocol.h"
#include "src/camera/drivers/controller/debug_protocol.h"

namespace camera {

class ControllerDevice;
using ControllerDeviceType = ddk::Device<ControllerDevice, ddk::Unbindable,
                                         ddk::Messageable<fuchsia_hardware_camera::Device>::Mixin>;
class ControllerDevice : public ControllerDeviceType,
                         public ddk::EmptyProtocol<ZX_PROTOCOL_CAMERA> {
 public:
  ~ControllerDevice() override;

  // Creates a new ControllerDevice using the provided parent.
  static fpromise::result<std::unique_ptr<ControllerDevice>, zx_status_t> Create(
      zx_device_t* parent);

  // ddk::Device mixin
  void DdkRelease();

  // ddk::Unbindable mixin
  void DdkUnbind(ddk::UnbindTxn txn);

  // Loads firmware from the given path.
  fpromise::result<std::pair<zx::vmo, size_t>, zx_status_t> LoadFirmware(const std::string& path);

 private:
  explicit ControllerDevice(zx_device_t* parent);

  // fuchsia.hardware.camera.Device implementation
  void GetChannel(GetChannelRequestView request, GetChannelCompleter::Sync& completer) override;
  void GetChannel2(GetChannel2RequestView request, GetChannel2Completer::Sync& completer) override;
  void GetDebugChannel(GetDebugChannelRequestView request,
                       GetDebugChannelCompleter::Sync& completer) override;

  // Controller loop.
  async::Loop loop_;

  // Composite driver children.
  ddk::SysmemProtocolClient sysmem_;
  ddk::IspProtocolClient isp_;
  ddk::GdcProtocolClient gdc_;
  ddk::Ge2dProtocolClient ge2d_;

  // Serves the fuchsia.camera2.hal.Controller protocol
  std::unique_ptr<ControllerImpl> controller_;

  // Serves the fuchsia.camera2.debug.Debug protocol
  std::unique_ptr<DebugImpl> debug_;
};

}  // namespace camera

#endif  // SRC_CAMERA_DRIVERS_CONTROLLER_CONTROLLER_DEVICE_H_
