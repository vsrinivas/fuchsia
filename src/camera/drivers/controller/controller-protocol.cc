// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "controller-protocol.h"

#include "fuchsia/camera2/cpp/fidl.h"

namespace camera {

ControllerImpl::ControllerImpl(fidl::InterfaceRequest<fuchsia::camera2::hal::Controller> control,
                               async_dispatcher_t* dispatcher, fit::closure on_connection_closed)
    : binding_(this, std::move(control), dispatcher) {
  binding_.set_error_handler(
      [occ = std::move(on_connection_closed)](zx_status_t /*status*/) { occ(); });
  configs_ = SherlockConfigs();
}

void ControllerImpl::GetConfigs(GetConfigsCallback callback) {
  callback(fidl::Clone(configs_), ZX_OK);
}

void ControllerImpl::GetDeviceInfo(GetDeviceInfoCallback callback) {
  fuchsia::camera2::DeviceInfo camera_device_info;
  camera_device_info.set_vendor_name(kCameraVendorName);
  camera_device_info.set_product_name(kCameraProductName);
  camera_device_info.set_type(fuchsia::camera2::DeviceType::BUILTIN);
  callback(std::move(camera_device_info));
}

}  // namespace camera
