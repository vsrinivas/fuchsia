// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_CAMERA_DRIVERS_CONTROLLER_CONTROLLER_DEVICE_H_
#define SRC_CAMERA_DRIVERS_CONTROLLER_CONTROLLER_DEVICE_H_

#include <lib/ddk/platform-defs.h>
#ifndef _ALL_SOURCE
#define _ALL_SOURCE  // Enables thrd_create_with_name in <threads.h>.
#include <threads.h>
#endif

#include <fidl/fuchsia.hardware.camera/cpp/wire.h>
#include <fuchsia/hardware/gdc/cpp/banjo.h>
#include <fuchsia/hardware/ge2d/cpp/banjo.h>
#include <fuchsia/hardware/isp/cpp/banjo.h>
#include <fuchsia/hardware/platform/bus/c/banjo.h>
#include <fuchsia/hardware/platform/device/c/banjo.h>
#include <fuchsia/hardware/sysmem/cpp/banjo.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/async/cpp/wait.h>
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
  DISALLOW_COPY_AND_ASSIGN_ALLOW_MOVE(ControllerDevice);
  explicit ControllerDevice(zx_device_t* parent, zx::event event)
      : ControllerDeviceType(parent),
        isp_(parent, "isp"),
        gdc_(parent, "gdc"),
        ge2d_(parent, "ge2d"),
        shutdown_event_(std::move(event)),
        loop_(&kAsyncLoopConfigNoAttachToCurrentThread),
        sysmem_(parent, "sysmem") {}

  ~ControllerDevice() { ShutDown(); }

  // Setup() is used to create an instance of Controller.
  static zx_status_t Setup(zx_device_t* parent, std::unique_ptr<ControllerDevice>* out);

  // Methods required by the ddk.
  void DdkRelease();
  void DdkUnbind(ddk::UnbindTxn txn);

  // Used for tests.
  // Starts the async loop thread which is owned by the controller.
  zx_status_t StartThread();

 private:
  void ShutDown();

  // Fuchsia Hardware Camera FIDL implementation.
  void GetChannel2(GetChannel2RequestView request, GetChannel2Completer::Sync& completer) override;
  // Call not supported
  void GetChannel(GetChannelRequestView request, GetChannelCompleter::Sync& completer) override {
    completer.Close(ZX_ERR_NOT_SUPPORTED);
  }
  void GetDebugChannel(GetDebugChannelRequestView request,
                       GetDebugChannelCompleter::Sync& completer) override;

  ddk::IspProtocolClient isp_;
  ddk::GdcProtocolClient gdc_;
  ddk::Ge2dProtocolClient ge2d_;
  async::Wait shutdown_waiter_;
  zx::event shutdown_event_;
  async::Loop loop_;
  thrd_t loop_thread_;
  std::unique_ptr<ControllerImpl> controller_;
  std::unique_ptr<DebugImpl> debug_;
  ddk::SysmemProtocolClient sysmem_;
};

}  // namespace camera

#endif  // SRC_CAMERA_DRIVERS_CONTROLLER_CONTROLLER_DEVICE_H_
