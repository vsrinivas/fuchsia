// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/camera/bin/device/test/fake_controller.h"

#include <lib/async-loop/default.h>
#include <lib/syslog/cpp/logger.h>

static fuchsia::camera2::DeviceInfo DefaultDeviceInfo() {
  fuchsia::camera2::DeviceInfo device_info{};
  device_info.set_vendor_id(0xFFFF);
  device_info.set_vendor_name("Fake Vendor Name");
  device_info.set_product_id(0xABCD);
  device_info.set_product_name("Fake Product Name");
  device_info.set_type(fuchsia::camera2::DeviceType::VIRTUAL);
  return device_info;
}

fit::result<std::unique_ptr<FakeController>, zx_status_t> FakeController::Create(
    fidl::InterfaceRequest<fuchsia::camera2::hal::Controller> request) {
  auto controller = std::make_unique<FakeController>();

  zx_status_t status = controller->loop_.StartThread("Fake Controller Loop");
  if (status != ZX_OK) {
    FX_PLOGS(ERROR, status);
    return fit::error(status);
  }

  status = controller->binding_.Bind(std::move(request), controller->loop_.dispatcher());
  if (status != ZX_OK) {
    FX_PLOGS(ERROR, status);
    return fit::error(status);
  }

  return fit::ok(std::move(controller));
}

FakeController::FakeController()
    : loop_(&kAsyncLoopConfigNoAttachToCurrentThread), binding_(this) {}

FakeController::~FakeController() { loop_.Shutdown(); }

void FakeController::GetConfigs(fuchsia::camera2::hal::Controller::GetConfigsCallback callback) {
  callback({}, ZX_OK);
}

void FakeController::CreateStream(uint32_t config_index, uint32_t stream_index,
                                  uint32_t image_format_index,
                                  fuchsia::sysmem::BufferCollectionInfo_2 buffer_collection,
                                  fidl::InterfaceRequest<fuchsia::camera2::Stream> stream) {}

void FakeController::EnableStreaming() {}

void FakeController::DisableStreaming() {}

void FakeController::GetDeviceInfo(
    fuchsia::camera2::hal::Controller::GetDeviceInfoCallback callback) {
  callback(DefaultDeviceInfo());
}
