// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fcntl.h>
#include <fidl/fuchsia.hardware.camera/cpp/wire.h>
#include <fuchsia/camera/cpp/fidl.h>
#include <fuchsia/hardware/camera/cpp/fidl.h>
#include <fuchsia/ui/policy/cpp/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/async/cpp/executor.h>
#include <lib/async/cpp/wait.h>
#include <lib/fdio/cpp/caller.h>
#include <lib/fdio/directory.h>
#include <lib/fdio/fd.h>
#include <lib/fdio/fdio.h>
#include <lib/sys/cpp/component_context.h>
#include <lib/syslog/cpp/log_settings.h>
#include <lib/syslog/cpp/macros.h>
#include <lib/zx/result.h>

#include <string>

#include "src/camera/bin/usb_device/device_impl.h"

// Hard coded camera device path!
// TODO(ernesthua) - Need to dynamically determine the path name!
std::string camera_path("/dev/class/camera/000");

zx::result<fuchsia::camera::ControlSyncPtr> OpenCamera(std::string path) {
  fbl::unique_fd fd(open(path.c_str(), O_RDWR));
  if (fd.get() < 0) {
    FX_PLOGS(ERROR, fd.get()) << "Failed to open sensor at " << path;
    return zx::error(ZX_ERR_INTERNAL);
  }
  FX_LOGS(INFO) << "opened " << path;

  fuchsia::camera::ControlSyncPtr ctrl;
  fdio_cpp::UnownedFdioCaller caller(fd);
  auto status = fidl::WireCall(caller.borrow_as<fuchsia_hardware_camera::Device>())
                    ->GetChannel(ctrl.NewRequest().TakeChannel())
                    .status();
  if (status != ZX_OK) {
    FX_PLOGS(INFO, status) << "Couldn't GetChannel from " << path;
    return zx::error(status);
  }

  fuchsia::camera::DeviceInfo info_return;
  status = ctrl->GetDeviceInfo(&info_return);
  if (status != ZX_OK) {
    FX_PLOGS(ERROR, status) << "Could not get Device Info";
    return zx::error(status);
  }
  FX_LOGS(INFO) << "Got Device Info:";
  FX_LOGS(INFO) << "Vendor: " << info_return.vendor_name << " (" << info_return.vendor_id << ")";
  return zx::ok(std::move(ctrl));
}

int main(int argc, char* argv[]) {
  syslog::SetLogSettings({.min_log_level = CAMERA_MIN_LOG_LEVEL}, {"camera", "camera_device"});

  async::Loop loop(&kAsyncLoopConfigAttachToCurrentThread);
  async::Executor executor(loop.dispatcher());
  auto context = sys::ComponentContext::Create();

  // Removed argument parsing.
  // TODO(ernesthua) - Need to bring back argument parsing on merge back.
  std::string outgoing_service_name("fuchsia.camera3.Device");

  // Connect to hard coded usb camera device.
  zx::result<fuchsia::camera::ControlSyncPtr> status_or = OpenCamera(camera_path);
  if (status_or.is_error()) {
    FX_PLOGS(FATAL, status_or.error_value())
        << "Failed to request camera device: error: " << status_or.error_value();
    return EXIT_FAILURE;
  }
  auto control_sync_ptr = std::move(*(status_or));

  // Connect to required environment services.
  fuchsia::sysmem::AllocatorHandle allocator_handle;
  fuchsia::sysmem::AllocatorPtr allocator_ptr;
  zx_status_t status = context->svc()->Connect(allocator_handle.NewRequest());
  if (status != ZX_OK) {
    FX_PLOGS(FATAL, status) << "Failed to request allocator service.";
    return EXIT_FAILURE;
  }
  allocator_ptr = allocator_handle.Bind();

  // Post a quit task in the event the device enters a bad state.
  zx::event event;
  FX_CHECK(zx::event::create(0, &event) == ZX_OK);
  async::Wait wait(event.get(), ZX_EVENT_SIGNALED, 0,
                   [&](async_dispatcher_t* dispatcher, async::Wait* wait, zx_status_t status,
                       const zx_packet_signal_t* signal) {
                     FX_LOGS(FATAL) << "Device signaled bad state.";
                     loop.Quit();
                   });
  ZX_ASSERT(wait.Begin(loop.dispatcher()) == ZX_OK);

  // Create the device and publish its service.
  auto result = camera::DeviceImpl::Create(loop.dispatcher(), executor, std::move(control_sync_ptr),
                                           std::move(allocator_ptr), std::move(event));
  std::unique_ptr<camera::DeviceImpl> device;
  executor.schedule_task(
      result.then([&context, &device, &loop, &outgoing_service_name](
                      fpromise::result<std::unique_ptr<camera::DeviceImpl>, zx_status_t>& result) {
        if (result.is_error()) {
          FX_PLOGS(FATAL, result.error()) << "Failed to create device.";
          loop.Quit();
          return;
        }
        device = result.take_value();

        // TODO(fxbug.dev/44628): publish discoverable service name once supported
        zx_status_t status =
            context->outgoing()->AddPublicService(device->GetHandler(), outgoing_service_name);
        if (status != ZX_OK) {
          FX_PLOGS(FATAL, status) << "Failed to publish service.";
          loop.Quit();
          return;
        }
        context->outgoing()->ServeFromStartupInfo();
      }));

  loop.Run();
  return EXIT_SUCCESS;
}
