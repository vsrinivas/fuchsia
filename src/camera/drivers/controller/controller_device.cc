// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/camera/drivers/controller/controller_device.h"

#include <lib/ddk/debug.h>
#include <lib/ddk/driver.h>
#include <lib/syslog/cpp/macros.h>
#include <stdint.h>
#include <zircon/threads.h>
#include <zircon/types.h>

#include <ddktl/fidl.h>

#include "src/camera/drivers/controller/bind.h"
#include "src/lib/fsl/handles/object_info.h"

namespace camera {

constexpr auto kTag = "camera_controller";

void ControllerDevice::DdkUnbind(ddk::UnbindTxn txn) {
  ShutDown();
  txn.Reply();
}

void ControllerDevice::DdkRelease() { delete this; }

void ControllerDevice::GetChannel2(GetChannel2RequestView request,
                                   GetChannel2Completer::Sync& completer) {
  if (!request->server_end.is_valid()) {
    completer.Close(ZX_ERR_INVALID_ARGS);
    return;
  }

  if (controller_ != nullptr) {
    zxlogf(ERROR, "%s: Camera2 Controller already running", __func__);
    completer.Close(ZX_ERR_INTERNAL);
    return;
  }

  fidl::InterfaceRequest<fuchsia::camera2::hal::Controller> control_interface(
      request->server_end.TakeChannel());

  fuchsia::sysmem::AllocatorSyncPtr sysmem_allocator;

  auto status = sysmem_.Connect(sysmem_allocator.NewRequest().TakeChannel());
  if (status != ZX_OK) {
    FX_PLOGST(ERROR, kTag, status) << "Could not setup sysmem allocator";
    completer.Close(status);
    return;
  }

  sysmem_allocator->SetDebugClientInfo(fsl::GetCurrentProcessName(), fsl::GetCurrentProcessKoid());

  auto shutdown_callback = [this] {
    shutdown_waiter_.set_handler([this](async_dispatcher_t* dispatcher, async::Wait* wait,
                                        zx_status_t status, const zx_packet_signal_t* signal) {
      controller_ = nullptr;
      // Clear the signal.
      shutdown_event_.signal(kPipelineManagerSignalExitDone, 0u);
    });

    shutdown_waiter_.set_object(shutdown_event_.get());
    shutdown_waiter_.set_trigger(kPipelineManagerSignalExitDone);
    shutdown_waiter_.Begin(loop_.dispatcher());

    controller_->Shutdown();
  };

  if (control_interface.is_valid()) {
    controller_ = std::make_unique<ControllerImpl>(
        parent(), std::move(control_interface), loop_.dispatcher(), isp_, gdc_, ge2d_,
        shutdown_callback, std::move(sysmem_allocator), shutdown_event_);
    completer.Close(ZX_OK);
    return;
  }
  completer.Close(ZX_ERR_INTERNAL);
}

void ControllerDevice::GetDebugChannel(GetDebugChannelRequestView request,
                                       GetDebugChannelCompleter::Sync& completer) {
  if (debug_ != nullptr) {
    FX_LOGST(WARNING, kTag) << "Debug already running. Resetting ...";
    debug_ = nullptr;
  }

  if (!request->server_end.is_valid()) {
    completer.Close(ZX_ERR_INVALID_ARGS);
    return;
  }

  fidl::InterfaceRequest<fuchsia::camera2::debug::Debug> control_interface(
      request->server_end.TakeChannel());

  auto shutdown_callback = [this] {
    shutdown_waiter_.set_handler([this](async_dispatcher_t* dispatcher, async::Wait* wait,
                                        zx_status_t status,
                                        const zx_packet_signal_t* signal) { debug_ = nullptr; });
  };

  if (control_interface.is_valid()) {
    debug_ = std::make_unique<DebugImpl>(parent(), std::move(control_interface), loop_.dispatcher(),
                                         isp_, shutdown_callback);
    completer.Close(ZX_OK);
    return;
  }
  completer.Close(ZX_ERR_INTERNAL);
}

void ControllerDevice::ShutDown() { loop_.Shutdown(); }

zx_status_t ControllerDevice::StartThread() {
  return loop_.StartThread("camera-controller-loop", &loop_thread_);
}

// static
zx_status_t ControllerDevice::Setup(zx_device_t* parent, std::unique_ptr<ControllerDevice>* out) {
  ddk::GdcProtocolClient gdc(parent, "gdc");
  if (!gdc.is_valid()) {
    zxlogf(ERROR, "%s: ZX_PROTOCOL_GDC not available", __func__);
    return ZX_ERR_NO_RESOURCES;
  }

  ddk::Ge2dProtocolClient ge2d(parent, "ge2d");
  if (!ge2d.is_valid()) {
    zxlogf(ERROR, "%s: ZX_PROTOCOL_GE2D not available", __func__);
    return ZX_ERR_NO_RESOURCES;
  }

  ddk::IspProtocolClient isp(parent, "isp");
  if (!isp.is_valid()) {
    zxlogf(ERROR, "%s: ZX_PROTOCOL_ISP not available", __func__);
    return ZX_ERR_NO_RESOURCES;
  }

  ddk::SysmemProtocolClient sysmem(parent, "sysmem");
  if (!sysmem.is_valid()) {
    zxlogf(ERROR, "%s: ZX_PROTOCOL_SYSMEM not available", __func__);
    return ZX_ERR_NO_RESOURCES;
  }

  zx::event event;
  auto status = zx::event::create(0, &event);
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s: Could not create shutdown event", __func__);
    return status;
  }

  auto controller = std::make_unique<ControllerDevice>(parent, std::move(event));

  status = controller->StartThread();
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s: Could not start loop thread", __func__);
    return status;
  }

  *out = std::move(controller);
  return ZX_OK;
}

zx_status_t ControllerDeviceBind(void* /*ctx*/, zx_device_t* device) {
  std::unique_ptr<ControllerDevice> controller_device;
  auto status = camera::ControllerDevice::Setup(device, &controller_device);
  if (status != ZX_OK) {
    FX_PLOGST(ERROR, kTag, status) << "Could not setup camera_controller_device";
    return status;
  }

  status = controller_device->DdkAdd("camera-controller-device");
  if (status != ZX_OK) {
    FX_PLOGST(ERROR, kTag, status) << "Could not add camera_controller_device device";
    return status;
  }

  FX_LOGST(INFO, kTag) << "camera_controller_device driver added";

  // controller device intentionally leaked as it is now held by DevMgr.
  __UNUSED auto* dev = controller_device.release();
  return status;
}

static constexpr zx_driver_ops_t driver_ops = []() {
  zx_driver_ops_t ops = {};
  ops.version = DRIVER_OPS_VERSION;
  ops.bind = ControllerDeviceBind;
  return ops;
}();

}  // namespace camera

ZIRCON_DRIVER(camera_controller, camera::driver_ops, "fuchsia", "0.1");
