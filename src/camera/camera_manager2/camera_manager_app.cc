// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/camera/camera_manager2/camera_manager_app.h"

#include <fcntl.h>
#include <fuchsia/hardware/camera/c/fidl.h>
#include <lib/fdio/directory.h>

#include <string>

#include <ddk/debug.h>
#include <ddk/driver.h>
#include <fbl/auto_call.h>
#include <fbl/function.h>

#include "src/lib/syslog/cpp/logger.h"

namespace camera {

std::unique_ptr<CameraManagerApp> CameraManagerApp::Create(
    std::unique_ptr<sys::ComponentContext> context) {
  FX_LOGS(INFO) << "Starting";

  auto camera_manager = std::make_unique<CameraManagerApp>(std::move(context));

  zx_status_t status =
      camera_manager->context_->svc()->Connect(camera_manager->sysmem_allocator_.NewRequest());
  if (status != ZX_OK) {
    FX_LOGS(ERROR) << "Failed to connect to sysmem service. status " << status;
    return nullptr;
  }

  // Begin monitoring for plug/unplug events for pluggable cameras.
  status = camera_manager->plug_detector_.Start(
      fbl::BindMember(camera_manager.get(), &CameraManagerApp::OnDeviceFound));
  if (status != ZX_OK) {
    FX_LOGS(ERROR) << "Failed to start plug_detector. status " << status;
    return nullptr;
  }

  camera_manager->context_->outgoing()->AddPublicService<fuchsia::camera2::Manager>(
      camera_manager->bindings_.GetHandler(camera_manager.get()));
  return camera_manager;
}

// The dispatcher loop should be shut down when this destructor is called.
// No further messages should be handled after this destructor is called.
CameraManagerApp::~CameraManagerApp() {
  // Stop monitoring plug/unplug events.  We are shutting down and
  // no longer care about devices coming and going.
  plug_detector_.Stop();
}

void CameraManagerApp::OnDeviceFound(
    fidl::InterfaceHandle<fuchsia::camera2::hal::Controller> controller) {
  auto device = VideoDeviceClient::Create(std::move(controller));
  if (!device) {
    FX_LOGS(ERROR) << "Failed to create device";
    return;
  }
  // The device is fully set up at this point. The camera manager now needs to
  // assign a camera_id.
  // TODO(41683): If a device's info marks it as a previously known device,
  // it should be merged with that device instance.  That will allow the device
  // to maintain its id, mute status, and any other properties that must be retained
  // across boot.

  device->set_id(device_id_counter_++);
  devices_.push_back(std::move(device));
}

VideoDeviceClient *CameraManagerApp::GetActiveDevice(int32_t camera_id) {
  for (auto &device : devices_) {
    if (device->id() == camera_id) {
      return device.get();
    }
  }
  return nullptr;
}

// Currently, this function handles all the connections itself, and only
// connects to the first stream and format of the device.
void CameraManagerApp::ConnectToStream(
    int32_t camera_id, fuchsia::camera2::StreamConstraints constraints,
    fidl::InterfaceHandle<fuchsia::sysmem::BufferCollectionToken> token,
    fidl::InterfaceRequest<fuchsia::camera2::Stream> client_request,
    fuchsia::camera2::Manager::ConnectToStreamCallback callback) {
  // Create a cleanup function, so we can just return if we get an error.
  auto cleanup = fbl::MakeAutoCall([&callback]() {
    FX_LOGS(ERROR) << "Failed to connect to stream";
    ::fuchsia::sysmem::ImageFormat_2 ret;
    callback(ret);
  });
  // 1: Check that the camera exists:
  auto device = GetActiveDevice(camera_id);
  if (!device) {
    return;
  }

  uint32_t config_index = 0;
  uint32_t stream_type = 0;
  uint32_t image_format_index = constraints.has_format_index() ? constraints.format_index() : 0;

  // 2: Check constraints against the configs that the device offers.  If incompatible, fail.
  // 3: Pick a config, stream and image_format_index
  zx_status_t status = device->MatchConstraints(constraints, &config_index, &stream_type);
  if (status != ZX_OK) {
    FXL_LOG(ERROR) << "Failed to match constraints. status: " << status;
    return;
  }
  FX_LOGS(INFO) << "Picked config " << config_index << " stream index: " << stream_type
                << " format index: " << image_format_index;
  // Get configs from the device:
  auto &out_configs = device->configs();
  ZX_ASSERT(out_configs.size() > config_index);
  ZX_ASSERT(out_configs[config_index].stream_configs.size() > stream_type);
  auto &stream_config = out_configs[config_index].stream_configs[stream_type];

  // 4: Now check if the stream is currently being used.  If it is, we could:
  //     - A) Close the other stream
  //     - B) Have two clients of one stream
  //     - C) Choose another compatible stream
  //     - D) Refuse this request.
  // For now, we will do:
  //       E) Don't even check
  // TODO(36260): Check if other streams are active, and apply some policy.

  // 5: Allocate the buffer collection.  The constraints from the device must be applied, as well as
  // constraints for all the image formats being offered.  These should be checked at some point by
  // the camera manager.
  fidl::InterfaceHandle<fuchsia::sysmem::BufferCollection> sysmem_collection;
  status =
      sysmem_allocator_->BindSharedCollection(std::move(token), sysmem_collection.NewRequest());
  if (status != ZX_OK) {
    FXL_LOG(ERROR) << "Failed to connect to BindSharedCollection.";
    return;
  }

  // Now connect the client directly to the device
  status = device->CreateStream(config_index, stream_type, image_format_index,
                                std::move(sysmem_collection), std::move(client_request));
  if (status != ZX_OK) {
    FX_LOGS(ERROR) << "Failed to connect to create stream.";
    return;
  }

  // Return the image format that was selected.
  callback(stream_config.image_formats[image_format_index]);
  cleanup.cancel();
}

}  // namespace camera
