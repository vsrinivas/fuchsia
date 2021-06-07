// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/camera/bin/virtual_camera/virtual_camera_hal_controller.h"

#include <lib/syslog/cpp/macros.h>

namespace camera {

// Values used to create the |fuchsia::camera2::DeviceInfo| value returned in
// |GetDeviceInfo|.
const int kDeviceVendorId = 1;
const int kDeviceProductId = 2;
const char kDeviceVendorName[] = "virtual_camera_vendor";
const char kDeviceProductName[] = "virtual_camera_product";

VirtualCameraHalController::VirtualCameraHalController(
    StreamStorage& stream_storage,
    fidl::InterfaceRequest<fuchsia::camera2::hal::Controller> request)
    : stream_storage_(stream_storage), binding_(this, std::move(request)) {}

void VirtualCameraHalController::GetNextConfig(
    fuchsia::camera2::hal::Controller::GetNextConfigCallback callback) {
  if (get_next_config_called_) {
    callback(nullptr, ZX_ERR_STOP);
    return;
  }

  fuchsia::camera2::hal::Config config = stream_storage_.GetConfig();
  get_next_config_called_ = true;
  callback(std::make_unique<fuchsia::camera2::hal::Config>(std::move(config)), ZX_OK);
}

void VirtualCameraHalController::CreateStream(
    uint32_t config_index, uint32_t stream_index, uint32_t image_format_index,
    fidl::InterfaceRequest<fuchsia::camera2::Stream> stream) {
  FX_LOGS(WARNING) << "VirtualCameraHalController::CreateStream unimplemented";
}

void VirtualCameraHalController::EnableStreaming() {
  FX_LOGS(WARNING) << "VirtualCameraHalController::EnableStreaming unimplemented";
}

void VirtualCameraHalController::DisableStreaming() {
  FX_LOGS(WARNING) << "VirtualCameraHalController::DisableStreaming unimplemented";
}

void VirtualCameraHalController::GetDeviceInfo(
    fuchsia::camera2::hal::Controller::GetDeviceInfoCallback callback) {
  fuchsia::camera2::DeviceInfo device_info;
  device_info.set_vendor_id(kDeviceVendorId);
  device_info.set_vendor_name(kDeviceVendorName);
  device_info.set_product_id(kDeviceProductId);
  device_info.set_product_name(kDeviceProductName);
  device_info.set_type(fuchsia::camera2::DeviceType::VIRTUAL);
  callback(std::move(device_info));
}

}  // namespace camera
