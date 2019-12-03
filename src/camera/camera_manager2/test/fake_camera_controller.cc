// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "fake_camera_controller.h"

#include <fuchsia/camera2/cpp/fidl.h>
#include <fuchsia/camera2/hal/cpp/fidl.h>
#include <fuchsia/sysmem/cpp/fidl.h>
#include <lib/fidl/cpp/binding.h>

#include <vector>

#include "src/camera/stream_utils/stream_constraints.h"
#include "src/lib/syslog/cpp/logger.h"

namespace camera {

std::vector<fuchsia::camera2::hal::Config> FakeController::InvalidConfigs() {
  std::vector<fuchsia::camera2::hal::Config> configs(2);
  return configs;
}

std::vector<fuchsia::camera2::hal::Config> FakeController::StandardConfigs() {
  std::vector<fuchsia::camera2::hal::Config> configs(2);

  StreamConstraints ml_mon_stream(fuchsia::camera2::CameraStreamType::MACHINE_LEARNING |
                                  fuchsia::camera2::CameraStreamType::MONITORING);
  ml_mon_stream.AddImageFormat(640, 512, fuchsia::sysmem::PixelFormatType::NV12);
  configs[0].stream_configs.push_back(ml_mon_stream.ConvertToStreamConfig());

  StreamConstraints mon_stream(fuchsia::camera2::CameraStreamType::MONITORING);
  mon_stream.AddImageFormat(640, 512, fuchsia::sysmem::PixelFormatType::NV12);
  mon_stream.AddImageFormat(896, 1600, fuchsia::sysmem::PixelFormatType::NV12);
  configs[0].stream_configs.push_back(mon_stream.ConvertToStreamConfig());

  StreamConstraints ml_vid_stream(fuchsia::camera2::CameraStreamType::MACHINE_LEARNING |
                                  fuchsia::camera2::CameraStreamType::VIDEO_CONFERENCE);
  ml_vid_stream.AddImageFormat(640, 512, fuchsia::sysmem::PixelFormatType::NV12);
  configs[1].stream_configs.push_back(ml_vid_stream.ConvertToStreamConfig());

  StreamConstraints vid_stream(fuchsia::camera2::CameraStreamType::VIDEO_CONFERENCE);
  vid_stream.AddImageFormat(640, 512, fuchsia::sysmem::PixelFormatType::NV12);
  vid_stream.AddImageFormat(2176, 2720, fuchsia::sysmem::PixelFormatType::NV12);
  configs[1].stream_configs.push_back(vid_stream.ConvertToStreamConfig());

  return configs;
}

static bool CheckMatchingChannel(const zx::channel& client_side, const zx::channel& server_side) {
  zx_info_handle_basic_t info[2];
  zx_status_t status = client_side.get_info(ZX_INFO_HANDLE_BASIC, &info[0],
                                            sizeof(zx_info_handle_basic_t), nullptr, nullptr);
  if (status != ZX_OK) {
    FX_LOGS(ERROR) << "Failed to get client side koid.";
    return false;
  }
  status = server_side.get_info(ZX_INFO_HANDLE_BASIC, &info[1], sizeof(zx_info_handle_basic_t),
                                nullptr, nullptr);
  if (status != ZX_OK || info[1].koid == 0) {
    FX_LOGS(ERROR) << "Failed to get server side koid.";
    return false;
  }
  return (info[0].related_koid == info[1].koid) && (info[1].related_koid == info[0].koid);
}

FakeController::FakeController(std::vector<fuchsia::camera2::hal::Config> configs,
                               async_dispatcher_t* dispatcher)
    : configs_(std::move(configs)), dispatcher_(dispatcher) {}

fidl::InterfaceHandle<fuchsia::camera2::hal::Controller> FakeController::GetCameraConnection() {
  ZX_ASSERT(!binding_.is_bound());
  fidl::InterfaceHandle<fuchsia::camera2::hal::Controller> client;
  binding_.Bind(client.NewRequest(), dispatcher_);
  return client;
}

void FakeController::GetConfigs(GetConfigsCallback callback) {
  // Make up some configs:
  std::vector<fuchsia::camera2::hal::Config> empty_configs;
  switch (configs_failure_) {
    case GetConfigsFailureMode::VALID_CONFIGS:
      callback(fidl::Clone(configs_), ZX_OK);
      break;
    case GetConfigsFailureMode::RETURNS_ERROR:
      callback(fidl::Clone(configs_), ZX_ERR_INTERNAL);
      break;
    case GetConfigsFailureMode::EMPTY_VECTOR:
      callback(std::move(empty_configs), ZX_OK);
      break;
    case GetConfigsFailureMode::INVALID_CONFIG:
      callback(InvalidConfigs(), ZX_ERR_INTERNAL);
      break;
  }
}

void FakeController::CreateStream(uint32_t config_index, uint32_t stream_index,
                                  uint32_t image_format_index,
                                  ::fuchsia::sysmem::BufferCollectionInfo_2 buffer_collection,
                                  ::fidl::InterfaceRequest<::fuchsia::camera2::Stream> stream) {
  CameraConnection conn = {.config_index = config_index,
                           .stream_index = stream_index,
                           .image_format_index = image_format_index,
                           .buffer_collection = std::move(buffer_collection),
                           .stream = std::move(stream)};
  connections_.push_back(std::move(conn));
}

void FakeController::GetDeviceInfo(GetDeviceInfoCallback callback) {
  const char* kVirtualCameraVendorName = "Google Inc.";
  const char* kVirtualCameraProductName = "Fuchsia Fake Camera";
  fuchsia::camera2::DeviceInfo camera_device_info;
  camera_device_info.set_vendor_name(kVirtualCameraVendorName);
  camera_device_info.set_product_name(kVirtualCameraProductName);
  camera_device_info.set_type(fuchsia::camera2::DeviceType::VIRTUAL);
  callback(std::move(camera_device_info));
}

bool FakeController::HasMatchingChannel(const zx::channel& client_side) const {
  for (auto& connection : connections_) {
    if (CheckMatchingChannel(connection.stream.channel(), client_side)) {
      return true;
    }
  }
  return false;
}

}  // namespace camera
