// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_CAMERA_DRIVERS_CONTROLLER_CONTROLLER_DEVICE_H_
#define SRC_CAMERA_DRIVERS_CONTROLLER_CONTROLLER_DEVICE_H_

#include <ddk/platform-defs.h>
#ifndef _ALL_SOURCE
#define _ALL_SOURCE  // Enables thrd_create_with_name in <threads.h>.
#include <threads.h>
#endif

#include <fuchsia/hardware/camera/c/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/async/cpp/wait.h>
#include <lib/device-protocol/pdev.h>
#include <lib/device-protocol/platform-device.h>
#include <lib/fidl-utils/bind.h>
#include <zircon/fidl.h>

#include <ddk/protocol/platform/bus.h>
#include <ddk/protocol/platform/device.h>
#include <ddktl/device.h>
#include <ddktl/protocol/composite.h>
#include <ddktl/protocol/empty-protocol.h>
#include <ddktl/protocol/gdc.h>
#include <ddktl/protocol/ge2d.h>
#include <ddktl/protocol/isp.h>
#include <ddktl/protocol/sysmem.h>

#include "src/camera/drivers/controller/controller_protocol.h"

namespace camera {

class ControllerDevice;
using ControllerDeviceType = ddk::Device<ControllerDevice, ddk::Unbindable, ddk::Messageable>;

class ControllerDevice : public ControllerDeviceType,
                         public ddk::EmptyProtocol<ZX_PROTOCOL_CAMERA> {
 public:
  DISALLOW_COPY_AND_ASSIGN_ALLOW_MOVE(ControllerDevice);
  explicit ControllerDevice(zx_device_t* parent, zx_device_t* isp, zx_device_t* gdc,
                            zx_device_t* ge2d, zx_device_t* sysmem, zx::event event)
      : ControllerDeviceType(parent),
        isp_(isp),
        gdc_(gdc),
        ge2d_(ge2d),
        shutdown_event_(std::move(event)),
        loop_(&kAsyncLoopConfigNoAttachToCurrentThread),
        sysmem_(sysmem) {}

  ~ControllerDevice() { ShutDown(); }

  // Setup() is used to create an instance of Controller.
  static zx_status_t Setup(zx_device_t* parent, std::unique_ptr<ControllerDevice>* out);

  // Methods required by the ddk.
  void DdkRelease();
  void DdkUnbind(ddk::UnbindTxn txn);
  zx_status_t DdkMessage(fidl_incoming_msg_t* msg, fidl_txn_t* txn);

  // Used for tests.
  // Starts the async loop thread which is owned by the controller.
  zx_status_t StartThread();

 private:
  void ShutDown();

  // Fuchsia Hardware Camera FIDL implementation.
  zx_status_t GetChannel2(zx_handle_t handle);
  zx_status_t GetChannel(zx_handle_t handle) {
    // Closing the handle to let the client know that this call is not supported.
    zx::channel ch(handle);
    return ZX_ERR_NOT_SUPPORTED;
  }

  static constexpr fuchsia_hardware_camera_Device_ops_t fidl_ops = {
      .GetChannel = fidl::Binder<ControllerDevice>::BindMember<&ControllerDevice::GetChannel>,
      .GetChannel2 = fidl::Binder<ControllerDevice>::BindMember<&ControllerDevice::GetChannel2>,
  };

  ddk::IspProtocolClient isp_;
  ddk::GdcProtocolClient gdc_;
  ddk::Ge2dProtocolClient ge2d_;
  async::Wait shutdown_waiter_;
  zx::event shutdown_event_;
  async::Loop loop_;
  thrd_t loop_thread_;
  std::unique_ptr<ControllerImpl> controller_;
  ddk::SysmemProtocolClient sysmem_;
};

}  // namespace camera

#endif  // SRC_CAMERA_DRIVERS_CONTROLLER_CONTROLLER_DEVICE_H_
