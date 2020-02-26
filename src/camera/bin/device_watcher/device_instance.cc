// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/camera/bin/device_watcher/device_instance.h"

#include <fuchsia/camera2/hal/cpp/fidl.h>
#include <lib/sys/service/cpp/service.h>
#include <lib/syslog/cpp/logger.h>
#include <zircon/errors.h>

#include "fuchsia/io/cpp/fidl.h"
#include "lib/fit/function.h"
#include "lib/sys/cpp/service_directory.h"

// Known path for the camera_device package manifest.
constexpr auto kCameraDeviceUrl = "fuchsia-pkg://fuchsia.com/camera_device#meta/manifest.cmx";

// Arbitrary string identifying the camera device protocol. The protocol does not have a fixed name
// because it is not marked as [Discoverable].
constexpr auto kCameraPublishedServiceName = "PublishedCameraService";

DeviceInstance::DeviceInstance() : loop_(&kAsyncLoopConfigNoAttachToCurrentThread) {}

fit::result<std::unique_ptr<DeviceInstance>, zx_status_t> DeviceInstance::Create(
    const fuchsia::sys::LauncherPtr& launcher,
    fidl::InterfaceHandle<fuchsia::hardware::camera::Device> camera,
    fit::closure on_component_unavailable) {
  auto instance = std::make_unique<DeviceInstance>();

  // Bind the camera channel.
  ZX_ASSERT(instance->camera_.Bind(std::move(camera), instance->loop_.dispatcher()) == ZX_OK);

  // Add the camera controller as an injected service.
  fidl::InterfaceRequestHandler<fuchsia::camera2::hal::Controller> handler =
      fit::bind_member(instance.get(), &DeviceInstance::OnControllerRequested);
  zx_status_t status = instance->injected_services_dir_.AddEntry(
      fuchsia::camera2::hal::Controller::Name_, std::make_unique<vfs::Service>(std::move(handler)));
  if (status != ZX_OK) {
    FX_PLOGS(ERROR, status);
    return fit::error(status);
  }
  auto additional_services = fuchsia::sys::ServiceList::New();
  additional_services->names.push_back(fuchsia::camera2::hal::Controller::Name_);

  // Bind the injected services directory to the given channel.
  fidl::InterfaceHandle<fuchsia::io::Directory> injected_services_dir_channel;
  status = instance->injected_services_dir_.Serve(
      fuchsia::io::OPEN_RIGHT_READABLE | fuchsia::io::OPEN_RIGHT_WRITABLE,
      injected_services_dir_channel.NewRequest().TakeChannel());
  if (status != ZX_OK) {
    FX_PLOGS(ERROR, status);
    return fit::error(status);
  }

  // Create a service directory into which the component will publish services.
  zx::channel directory_request;
  instance->published_services_ = sys::ServiceDirectory::CreateWithRequest(&directory_request);

  // Launch the Device component using the injected services channel.
  fuchsia::sys::LaunchInfo launch_info{};
  launch_info.url = kCameraDeviceUrl;
  launch_info.arguments = {kCameraPublishedServiceName};
  launch_info.directory_request = std::move(directory_request);
  launch_info.additional_services = std::move(additional_services);
  launch_info.additional_services->host_directory = injected_services_dir_channel.TakeChannel();
  launcher->CreateComponent(std::move(launch_info), instance->component_controller_.NewRequest(
                                                        instance->loop_.dispatcher()));

  // Bind component event handlers.
  instance->component_controller_.events().OnDirectoryReady =
      fit::bind_member(instance.get(), &DeviceInstance::OnServicesReady);
  instance->component_controller_.events().OnTerminated =
      [callback = on_component_unavailable.share()](int64_t return_code,
                                                    fuchsia::sys::TerminationReason reason) {
        FX_LOGS(ERROR) << "Camera Device Component exited with code " << return_code
                       << " and reason " << static_cast<uint32_t>(reason);
        callback();
      };

  // Bind error handlers.
  instance->component_controller_.set_error_handler(
      [callback = on_component_unavailable.share()](zx_status_t status) {
        FX_PLOGS(ERROR, status) << "Component controller disconnected.";
        // Invoke the callback here since OnTerminated won't be called.
        callback();
      });
  instance->camera_.set_error_handler([instance = instance.get()](zx_status_t status) {
    FX_PLOGS(WARNING, status) << "Camera device server disconnected.";
    instance->camera_ = nullptr;
  });

  // Start the loop.
  ZX_ASSERT(instance->loop_.StartThread("Camera Device Component Instance Thread") == ZX_OK);

  return fit::ok(std::move(instance));
}

void DeviceInstance::OnCameraRequested(fidl::InterfaceRequest<fuchsia::camera3::Device> request) {
  if (!services_ready_) {
    pending_requests_.push_back(std::move(request));
    return;
  }
  published_services_->Connect(std::move(request), kCameraPublishedServiceName);
}

void DeviceInstance::OnServicesReady() {
  services_ready_ = true;
  for (auto& request : pending_requests_) {
    OnCameraRequested(std::move(request));
  }
}

void DeviceInstance::OnControllerRequested(
    fidl::InterfaceRequest<fuchsia::camera2::hal::Controller> request) {
  if (!camera_) {
    request.Close(ZX_ERR_UNAVAILABLE);
    return;
  }
  camera_->GetChannel2(request.TakeChannel());
}
