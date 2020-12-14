// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/camera/lib/fake_controller/fake_controller.h"

#include <lib/async-loop/default.h>
#include <lib/async/cpp/task.h>
#include <lib/fidl/cpp/optional.h>
#include <lib/syslog/cpp/macros.h>
#include <zircon/errors.h>

#include "src/camera/lib/fake_legacy_stream/fake_legacy_stream.h"

static fuchsia::camera2::DeviceInfo DefaultDeviceInfo() {
  fuchsia::camera2::DeviceInfo device_info{};
  device_info.set_vendor_id(0xFFFF);
  device_info.set_vendor_name("Fake Vendor Name");
  device_info.set_product_id(0x0ABC);
  device_info.set_product_name("Fake Product Name");
  device_info.set_type(fuchsia::camera2::DeviceType::VIRTUAL);
  return device_info;
}

static std::vector<fuchsia::camera2::hal::Config> DefaultConfigs() {
  constexpr fuchsia::sysmem::BufferCollectionConstraints kBufferCollectionConstraints{
      .usage = {.cpu = fuchsia::sysmem::cpuUsageRead},
      .min_buffer_count_for_camping = 3,
      .image_format_constraints_count = 1,
      .image_format_constraints = {{{
          .pixel_format =
              {
                  .type = fuchsia::sysmem::PixelFormatType::NV12,
              },
          .color_spaces_count = 1,
          .color_space = {{{
              .type = fuchsia::sysmem::ColorSpaceType::REC601_NTSC,
          }}},
          .min_coded_width = 128,
          .max_coded_width = 4096,
          .min_coded_height = 1,
          .max_coded_height = 4096,
          .coded_width_divisor = 128,
      }}}};
  fuchsia::camera2::hal::Config config1;
  config1.stream_configs.push_back(
      {.frame_rate =
           {
               .frames_per_sec_numerator = 30,
               .frames_per_sec_denominator = 1,
           },
       .constraints = kBufferCollectionConstraints,
       .image_formats = {
           {
               .pixel_format =
                   kBufferCollectionConstraints.image_format_constraints[0].pixel_format,
               .coded_width = 1920,
               .coded_height = 1080,
               .bytes_per_row = 1920,
           },
           {
               .pixel_format =
                   kBufferCollectionConstraints.image_format_constraints[0].pixel_format,
               .coded_width = 1280,
               .coded_height = 720,
               .bytes_per_row = 1920,
           },
           {
               .pixel_format =
                   kBufferCollectionConstraints.image_format_constraints[0].pixel_format,
               .coded_width = 1024,
               .coded_height = 576,
               .bytes_per_row = 1920,
           }}});
  fuchsia::camera2::hal::Config config2;
  config2.stream_configs.push_back(
      {.frame_rate =
           {
               .frames_per_sec_numerator = 30,
               .frames_per_sec_denominator = 1,
           },
       .constraints = kBufferCollectionConstraints,
       .image_formats = {{
           .pixel_format = kBufferCollectionConstraints.image_format_constraints[0].pixel_format,
           .coded_width = 1280,
           .coded_height = 720,
           .bytes_per_row = 1280,
       }}});
  std::vector<fuchsia::camera2::hal::Config> configs;
  configs.push_back(std::move(config1));
  configs.push_back(std::move(config2));
  return configs;
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

std::vector<fuchsia::camera2::hal::Config> FakeController::GetDefaultConfigs() {
  return DefaultConfigs();
}

FakeController::FakeController()
    : loop_(&kAsyncLoopConfigNoAttachToCurrentThread), binding_(this) {}

FakeController::~FakeController() { loop_.Shutdown(); }

bool FakeController::LegacyStreamBufferIsOutstanding(uint32_t id) {
  bool is_outstanding = false;
  zx::event event;
  zx::event::create(0, &event);
  async::PostTask(loop_.dispatcher(), [&, this, id] {
    is_outstanding = stream_->IsOutstanding(id);
    event.signal(0, ZX_USER_SIGNAL_0);
  });
  event.wait_one(ZX_USER_SIGNAL_0, zx::time::infinite(), nullptr);
  return is_outstanding;
}

zx_status_t FakeController::SendFrameViaLegacyStream(fuchsia::camera2::FrameAvailableInfo info) {
  zx_status_t status = ZX_ERR_INTERNAL;
  zx::event event;
  zx::event::create(0, &event);
  async::PostTask(loop_.dispatcher(), [this, &status, &event, info = std::move(info)]() mutable {
    if (!stream_ || !stream_->IsStreaming()) {
      status = ZX_ERR_SHOULD_WAIT;
    } else {
      status = stream_->SendFrameAvailable(std::move(info));
    }
    event.signal(0, ZX_USER_SIGNAL_0);
  });
  event.wait_one(ZX_USER_SIGNAL_0, zx::time::infinite(), nullptr);
  return status;
}

void FakeController::GetNextConfig(
    fuchsia::camera2::hal::Controller::GetNextConfigCallback callback) {
  if (get_configs_call_count_ >= DefaultConfigs().size()) {
    callback(nullptr, ZX_ERR_STOP);
    return;
  }
  callback(fidl::MakeOptional(std::move(DefaultConfigs()[get_configs_call_count_++])), ZX_OK);
}

void FakeController::CreateStream(uint32_t config_index, uint32_t stream_index,
                                  uint32_t image_format_index,
                                  fuchsia::sysmem::BufferCollectionInfo_2 buffer_collection,
                                  fidl::InterfaceRequest<fuchsia::camera2::Stream> stream) {
  auto result = camera::FakeLegacyStream::Create(std::move(stream), 0, loop_.dispatcher());
  if (result.is_error()) {
    FX_PLOGS(ERROR, result.error());
    return;
  }
  stream_ = result.take_value();
}

void FakeController::EnableStreaming() {
  if (streaming_enabled_) {
    FX_LOGS(ERROR) << "Called EnableStreaming when already enabled.";
    binding_.Close(ZX_ERR_BAD_STATE);
    return;
  }
  streaming_enabled_ = true;
}

void FakeController::DisableStreaming() {
  if (!streaming_enabled_) {
    FX_LOGS(ERROR) << "Called DisableStreaming when already disabled.";
    binding_.Close(ZX_ERR_BAD_STATE);
    return;
  }
  streaming_enabled_ = false;
}

void FakeController::GetDeviceInfo(
    fuchsia::camera2::hal::Controller::GetDeviceInfoCallback callback) {
  callback(DefaultDeviceInfo());
}
