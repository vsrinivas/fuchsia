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
  fuchsia::camera2::hal::StreamConfig stream_config{
      .frame_rate =
          {
              .frames_per_sec_numerator = 30,
              .frames_per_sec_denominator = 1,
          },
      .constraints = kBufferCollectionConstraints,
      .image_formats = {{
          .pixel_format = kBufferCollectionConstraints.image_format_constraints[0].pixel_format,
          .coded_width = 1920,
          .coded_height = 1080,
          .bytes_per_row = 1920,
      }}};
  fuchsia::camera2::hal::Config config;
  config.stream_configs.push_back(std::move(stream_config));
  std::vector<fuchsia::camera2::hal::Config> configs;
  configs.push_back(std::move(config));
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

void FakeController::GetConfigs(fuchsia::camera2::hal::Controller::GetConfigsCallback callback) {
  callback(DefaultConfigs(), ZX_OK);
}

void FakeController::CreateStream(uint32_t config_index, uint32_t stream_index,
                                  uint32_t image_format_index,
                                  fuchsia::sysmem::BufferCollectionInfo_2 buffer_collection,
                                  fidl::InterfaceRequest<fuchsia::camera2::Stream> stream) {
  // Stash the channel so it's not closed immediately, but don't do anything with it.
  channels_.push_back(stream.TakeChannel());
}

void FakeController::EnableStreaming() {}

void FakeController::DisableStreaming() {}

void FakeController::GetDeviceInfo(
    fuchsia::camera2::hal::Controller::GetDeviceInfoCallback callback) {
  callback(DefaultDeviceInfo());
}
