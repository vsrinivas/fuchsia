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
#include <fbl/unique_fd.h>

#include "src/lib/syslog/cpp/logger.h"

namespace camera {

CameraManagerApp::~CameraManagerApp() {
  // Close the connection the the buffer collection, to avoid sysmem
  // throwing an error:
  sysmem_collection_->Close();
}

CameraManagerApp::CameraManagerApp(std::unique_ptr<sys::ComponentContext> context)
    : context_(std::move(context)) {
  FX_LOGS(INFO) << "CameraManager: Initializing";
  zx_status_t status = fdio_service_connect("/svc/fuchsia.sysmem.Allocator",
                                            sysmem_allocator_.NewRequest().TakeChannel().release());

  FX_CHECK(status == ZX_OK) << "Failed to connect to sysmem service. status " << status;
  FX_LOGS(INFO) << "CameraManager: connected to sysmem service";

  // For now, just connect to the one camera directly.
  // TODO(36251): discover devices dynamically with the DeviceWatcher.
  zx::channel local, remote;
  status = zx::channel::create(0u, &local, &remote);
  FX_CHECK(status == ZX_OK) << "Failed to create channel. status " << status;

  // connect to the device:
  status = fdio_service_connect("/dev/class/camera/000", remote.release());
  FX_CHECK(status == ZX_OK) << "Failed to connect to camera. status " << status;

  zx_status_t res = fuchsia_hardware_camera_DeviceGetChannel2(
      local.release(), camera_control_.NewRequest().TakeChannel().release());
  FX_CHECK(status == ZX_OK) << "Failed to obtain channel (res " << res << ")";

  FX_LOGS(INFO) << "CameraManager: connected to camera service";
  context_->outgoing()->AddPublicService<fuchsia::camera2::Manager>(bindings_.GetHandler(this));
}

// Currently, this function handles all the connections itself, and only
// connects to the first stream and format of the device.
void CameraManagerApp::ConnectToStream(
    int32_t /*camera_id*/, fuchsia::camera2::StreamConstraints constraints,
    fidl::InterfaceHandle<fuchsia::sysmem::BufferCollectionToken> token,
    fidl::InterfaceRequest<fuchsia::camera2::Stream> client_request,
    fuchsia::camera2::Manager::ConnectToStreamCallback callback) {
  // Create a cleanup function, so we can just return if we get an error.
  auto cleanup = fbl::MakeAutoCall([&callback]() {
    FX_LOGS(ERROR) << "Failed to connect to stream";
    ::fuchsia::sysmem::ImageFormat_2 ret;
    callback(ret);
  });
  // 1: Check that the camera exists.
  // TODO(36256): Verify the camera_id is a known camera.
  // 2: Check constraints against the configs that the device offers.  If incompatible, fail.
  // 3: Pick a config, stream and image_format_index

  // Get configs from the device:
  fidl::VectorPtr<fuchsia::camera2::hal::Config> out_configs;
  zx_status_t out_status;
  zx_status_t fidl_status = camera_control_->GetConfigs(&out_configs, &out_status);

  if (fidl_status != ZX_OK || out_status != ZX_OK || !out_configs.has_value()) {
    FX_LOGS(ERROR) << "Couldn't get Camera Configs. status: " << fidl_status
                   << "  out_status: " << out_status
                   << "  out_configs.has_value(): " << out_configs.has_value();
    return;
  }
  // TODO(36259): Verify that the constraints allow for all the image formats.

  // TODO(36255): Match constraints and configs
  uint32_t config_index = 0;
  uint32_t stream_type = 0;
  uint32_t image_format_index = 0;

  // Configs are good. See if any match the stream properties.  If so, use that
  // stream and config. Otherwise, grab the first stream of the first config.
  // TODO(36255): Fail to connect to Stream if matching stream type is not found.
  if (constraints.has_properties() && constraints.properties().has_stream_type()) {
    auto requested_stream_type = constraints.properties().stream_type();
    uint32_t temp_config_index = 0;
    for (auto &config : out_configs.value()) {
      uint32_t temp_stream_index = 0;
      for (auto &stream : config.stream_configs) {
        if (stream.properties.has_stream_type() &&
            stream.properties.stream_type() == requested_stream_type) {
          config_index = temp_config_index;
          stream_type = temp_stream_index;
          break;
        }
        temp_stream_index++;
      }
      temp_config_index++;
    }
  }
  FX_LOGS(INFO) << "Picked config " << config_index << " stream index: " << stream_type;
  ZX_ASSERT(out_configs.value().size() > config_index);
  ZX_ASSERT(out_configs.value()[config_index].stream_configs.size() > stream_type);
  auto &stream_config = out_configs.value()[config_index].stream_configs[stream_type];

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
    if (sysmem_collection_.is_bound()) {
    FX_LOGS(INFO) << "Closing previous collection.";
      sysmem_collection_->Close();
    }
  zx_status_t status =
      sysmem_allocator_->BindSharedCollection(std::move(token), sysmem_collection_.NewRequest());
  if (status != ZX_OK) {
    FX_LOGS(ERROR) << "Failed to connect to BindSharedCollection.";
    return;
  }

  status = sysmem_collection_->SetConstraints(true, stream_config.constraints);
  if (status != ZX_OK) {
    FX_LOGS(ERROR) << "Failed to connect to SetConstraints.";
    return;
  }

  zx_status_t allocation_status = ZX_OK;
  fuchsia::sysmem::BufferCollectionInfo_2 buffer_collection_info{};
  FX_LOGS(INFO) << "Waiting for buffers to be allocated.";
  status = sysmem_collection_->WaitForBuffersAllocated(&allocation_status, &buffer_collection_info);
  if (status != ZX_OK || allocation_status != ZX_OK) {
    FX_LOGS(ERROR) << "Failed to allocate buffers.";
    return;
  }
  FX_LOGS(INFO) << "Buffers are allocated.";

  // Now connect the client directly to the device
  camera_control_->CreateStream(config_index, stream_type, image_format_index,
                                std::move(buffer_collection_info), std::move(client_request));

  // Return the image format that was selected.
  callback(stream_config.image_formats[0]);
  cleanup.cancel();
}

}  // namespace camera
