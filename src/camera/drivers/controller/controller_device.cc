// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/camera/drivers/controller/controller_device.h"

#include <lib/syslog/cpp/macros.h>
#include <stdint.h>
#include <zircon/threads.h>
#include <zircon/types.h>

#include <ddk/binding.h>
#include <ddk/debug.h>
#include <ddk/driver.h>

namespace camera {

constexpr auto kTag = "camera_controller";

namespace {
enum {
  FRAGMENT_ISP,
  FRAGMENT_GDC,
  FRAGMENT_GE2D,
  FRAGMENT_SYSMEM,
  FRAGMENT_COUNT,
};
}  // namespace

void ControllerDevice::DdkUnbind(ddk::UnbindTxn txn) {
  ShutDown();
  txn.Reply();
}

void ControllerDevice::DdkRelease() { delete this; }

zx_status_t ControllerDevice::DdkMessage(fidl_incoming_msg_t* msg, fidl_txn_t* txn) {
  return fuchsia_hardware_camera_Device_dispatch(this, txn, msg, &fidl_ops);
}

zx_status_t ControllerDevice::GetChannel2(zx_handle_t handle) {
  if (handle == ZX_HANDLE_INVALID) {
    return ZX_ERR_INVALID_ARGS;
  }

  if (controller_ != nullptr) {
    zx::channel channel(handle);
    zxlogf(ERROR, "%s: Camera2 Controller already running", __func__);
    return ZX_ERR_INTERNAL;
  }

  zx::channel channel(handle);
  fidl::InterfaceRequest<fuchsia::camera2::hal::Controller> control_interface(std::move(channel));

  fuchsia::sysmem::AllocatorSyncPtr sysmem_allocator;

  auto status = sysmem_.Connect(sysmem_allocator.NewRequest().TakeChannel());
  if (status != ZX_OK) {
    FX_PLOGST(ERROR, kTag, status) << "Could not setup sysmem allocator";
    return status;
  }

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
    return ZX_OK;
  }
  return ZX_ERR_INTERNAL;
}

void ControllerDevice::ShutDown() { loop_.Shutdown(); }

zx_status_t ControllerDevice::StartThread() {
  return loop_.StartThread("camera-controller-loop", &loop_thread_);
}

// static
zx_status_t ControllerDevice::Setup(zx_device_t* parent, std::unique_ptr<ControllerDevice>* out) {
  ddk::CompositeProtocolClient composite(parent);
  if (!composite.is_valid()) {
    zxlogf(ERROR, "%s: could not get composite protocoln", __func__);
    return ZX_ERR_NOT_SUPPORTED;
  }

  zx_device_t* fragments[FRAGMENT_COUNT];
  size_t actual;
  composite.GetFragments(fragments, FRAGMENT_COUNT, &actual);
  if (actual != FRAGMENT_COUNT) {
    zxlogf(ERROR, "%s: Could not get fragments", __func__);
    return ZX_ERR_NOT_SUPPORTED;
  }

  ddk::GdcProtocolClient gdc(fragments[FRAGMENT_GDC]);
  if (!gdc.is_valid()) {
    zxlogf(ERROR, "%s: ZX_PROTOCOL_GDC not available", __func__);
    return ZX_ERR_NO_RESOURCES;
  }

  ddk::Ge2dProtocolClient ge2d(fragments[FRAGMENT_GE2D]);
  if (!ge2d.is_valid()) {
    zxlogf(ERROR, "%s: ZX_PROTOCOL_GE2D not available", __func__);
    return ZX_ERR_NO_RESOURCES;
  }

  ddk::IspProtocolClient isp(fragments[FRAGMENT_ISP]);
  if (!isp.is_valid()) {
    zxlogf(ERROR, "%s: ZX_PROTOCOL_ISP not available", __func__);
    return ZX_ERR_NO_RESOURCES;
  }

  ddk::SysmemProtocolClient sysmem(fragments[FRAGMENT_SYSMEM]);
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

  auto controller = std::make_unique<ControllerDevice>(
      parent, fragments[FRAGMENT_ISP], fragments[FRAGMENT_GDC], fragments[FRAGMENT_GE2D],
      fragments[FRAGMENT_SYSMEM], std::move(event));

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

// clang-format off
ZIRCON_DRIVER_BEGIN(camera_controller, camera::driver_ops, "camera_ctrl", "0.1", 2)
    BI_ABORT_IF(NE, BIND_PROTOCOL, ZX_PROTOCOL_COMPOSITE),
    BI_MATCH_IF(EQ, BIND_PLATFORM_DEV_DID, PDEV_DID_CAMERA_CONTROLLER),
ZIRCON_DRIVER_END(camera_controller)
