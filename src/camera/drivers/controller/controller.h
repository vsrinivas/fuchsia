// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_CAMERA_DRIVERS_CONTROLLER_CONTROLLER_H_
#define SRC_CAMERA_DRIVERS_CONTROLLER_CONTROLLER_H_

#include <ddk/platform-defs.h>
#ifndef _ALL_SOURCE
#define _ALL_SOURCE  // Enables thrd_create_with_name in <threads.h>.
#include <threads.h>
#endif
#include <lib/async-loop/cpp/loop.h>
#include <lib/device-protocol/pdev.h>
#include <lib/device-protocol/platform-device.h>
#include <lib/fidl-utils/bind.h>
#include <lib/syslog/global.h>
#include <zircon/fidl.h>

#include <ddk/protocol/platform/bus.h>
#include <ddk/protocol/platform/device.h>
#include <ddktl/device.h>
#include <ddktl/protocol/composite.h>
#include <ddktl/protocol/empty-protocol.h>
#include <ddktl/protocol/gdc.h>
#include <ddktl/protocol/isp.h>
#include <fbl/auto_lock.h>
#include <fbl/mutex.h>
#include <fbl/unique_ptr.h>
namespace camera {

class Controller;
using ControllerDeviceType = ddk::Device<Controller, ddk::Unbindable>;

class Controller : public ControllerDeviceType, public ddk::EmptyProtocol<ZX_PROTOCOL_CAMERA> {
 public:
  DISALLOW_COPY_AND_ASSIGN_ALLOW_MOVE(Controller);
  explicit Controller(zx_device_t* parent, zx_device_t* isp, zx_device_t* gdc)
      : ControllerDeviceType(parent),
        isp_(isp),
        gdc_(gdc),
        controller_loop_(&kAsyncLoopConfigNoAttachToThread) {}

  ~Controller() = default;

  // Setup() is used to create an instance of Controller.
  static zx_status_t Setup(zx_device_t* parent, std::unique_ptr<Controller>* out);

  // Methods required by the ddk.
  void DdkRelease();
  void DdkUnbind();

 private:
  void ShutDown();
  zx_status_t StartThread();

  ddk::IspProtocolClient isp_;
  ddk::GdcProtocolClient gdc_;
  async::Loop controller_loop_;
  thrd_t loop_thread_;
};

}  // namespace camera

#endif  // SRC_CAMERA_DRIVERS_CONTROLLER_CONTROLLER_H_
