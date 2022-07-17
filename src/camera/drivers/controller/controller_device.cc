// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/camera/drivers/controller/controller_device.h"

#include <lib/ddk/debug.h>
#include <lib/ddk/driver.h>
#include <lib/syslog/cpp/log_settings.h>
#include <lib/syslog/cpp/macros.h>
#include <zircon/types.h>

#include <ddktl/fidl.h>

#include "src/camera/drivers/controller/bind.h"
#include "src/lib/fsl/handles/object_info.h"

namespace camera {

ControllerDevice::ControllerDevice(zx_device_t* parent)
    : ControllerDeviceType(parent),
      loop_(&kAsyncLoopConfigNoAttachToCurrentThread),
      sysmem_(parent, "sysmem"),
      isp_(parent, "isp"),
      gdc_(parent, "gdc"),
      ge2d_(parent, "ge2d") {}

ControllerDevice::~ControllerDevice() { loop_.Shutdown(); }

fpromise::result<std::unique_ptr<ControllerDevice>, zx_status_t> ControllerDevice::Create(
    zx_device_t* parent) {
  std::unique_ptr<ControllerDevice> device(new ControllerDevice(parent));

  device->controller_ = std::make_unique<ControllerImpl>(
      device->loop_.dispatcher(), device->sysmem_, device->isp_, device->gdc_, device->ge2d_,
      fit::bind_member(device.get(), &ControllerDevice::LoadFirmware));
  device->debug_ = std::make_unique<DebugImpl>(device->loop_.dispatcher(), device->isp_);

  zx_status_t status = device->loop_.StartThread("camera-controller");
  if (status != ZX_OK) {
    return fpromise::error(status);
  }

  return fpromise::ok(std::move(device));
}

void ControllerDevice::DdkRelease() { delete this; }

void ControllerDevice::DdkUnbind(ddk::UnbindTxn txn) { txn.Reply(); }

fpromise::result<std::pair<zx::vmo, size_t>, zx_status_t> ControllerDevice::LoadFirmware(
    const std::string& path) {
  zx::vmo vmo;
  size_t size = 0;
  zx_status_t status = ::load_firmware(parent(), path.c_str(), vmo.reset_and_get_address(), &size);
  if (status != ZX_OK) {
    FX_PLOGS(ERROR, status) << "failed to laod firmware: " << path;
    return fpromise::error(status);
  }
  return fpromise::ok(std::make_pair(std::move(vmo), size));
}

void ControllerDevice::GetChannel(GetChannelRequestView request,
                                  GetChannelCompleter::Sync& completer) {
  // This method formerly served a now-removed protocol.
  completer.Close(ZX_ERR_NOT_SUPPORTED);
}

void ControllerDevice::GetChannel2(GetChannel2RequestView request,
                                   GetChannel2Completer::Sync& completer) {
  controller_->Connect(
      fidl::InterfaceRequest<fuchsia::camera2::hal::Controller>(request->server_end.TakeChannel()));
}

void ControllerDevice::GetDebugChannel(GetDebugChannelRequestView request,
                                       GetDebugChannelCompleter::Sync& completer) {
  debug_->Connect(
      fidl::InterfaceRequest<fuchsia::camera2::debug::Debug>(request->server_end.TakeChannel()));
}

static zx_status_t ControllerDeviceBind(void* /*ctx*/, zx_device_t* parent) {
  syslog::SetTags({"camera-controller"});

  auto result = camera::ControllerDevice::Create(parent);
  if (result.is_error()) {
    FX_PLOGS(ERROR, result.error()) << "Could not setup camera_controller_device";
    return result.error();
  }

  zx_status_t status = result.value()->DdkAdd("camera-controller-device");
  if (status != ZX_OK) {
    FX_PLOGS(ERROR, status) << "Could not add camera_controller_device device";
    return status;
  }

  FX_LOGS(INFO) << "camera_controller_device driver added";

  // controller device intentionally leaked as it is now held by DevMgr.
  __UNUSED auto* dev = result.take_value().release();
  return ZX_OK;
}

static constexpr zx_driver_ops_t driver_ops = []() {
  zx_driver_ops_t ops = {};
  ops.version = DRIVER_OPS_VERSION;
  ops.bind = ControllerDeviceBind;
  return ops;
}();

}  // namespace camera

ZIRCON_DRIVER(camera_controller, camera::driver_ops, "fuchsia", "0.1");
