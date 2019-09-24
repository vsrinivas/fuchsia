// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_CAMERA_DRIVERS_CONTROLLER_CONFIGS_SHERLOCK_ISP_DEBUG_CONFIG_H_
#define SRC_CAMERA_DRIVERS_CONTROLLER_CONFIGS_SHERLOCK_ISP_DEBUG_CONFIG_H_

#include <fuchsia/camera2/hal/cpp/fidl.h>

#include <vector>

#include "fuchsia/sysmem/cpp/fidl.h"

// This file lists down all the configurations Sherlock
// camera stack will be supporting.

namespace camera {

namespace {

// ISP Debug Stream Parameters
constexpr uint32_t kIspStreamMinBufferForCamping = 5;
constexpr uint32_t kIspStreamMinWidth = 640;
constexpr uint32_t kIspStreamMaxWidth = 2048;
constexpr uint32_t kIspStreamMinHeight = 480;
constexpr uint32_t kIspStreamMaxHeight = 1280;
constexpr uint32_t kIspStreamMinBytesPerRow = 480;
constexpr uint32_t kIspStreamMaxBytesPerRow = 0xfffffff;
constexpr uint32_t kIspStreamLayers = 1;
constexpr uint32_t kIspStreamBytesPerRowDivisor = 128;
constexpr uint32_t kIspStreamColorSpaceCount = 1;
constexpr uint32_t kIspStreamWidth = 1920;
constexpr uint32_t kIspStreamHeight = 1080;
constexpr uint32_t kIspStreamFrameRate = 30;
constexpr ::fuchsia::sysmem::PixelFormatType kIspStreamPixelFormat =
    fuchsia::sysmem::PixelFormatType::NV12;
constexpr ::fuchsia::sysmem::ColorSpaceType kIspStreamColorSpaceType =
    fuchsia::sysmem::ColorSpaceType::REC601_PAL;
}  // namespace

static constexpr fuchsia::sysmem::BufferCollectionConstraints IspStreamConstraints() {
  fuchsia::sysmem::BufferCollectionConstraints constraints;
  constraints.min_buffer_count_for_camping = kIspStreamMinBufferForCamping;
  constraints.image_format_constraints_count = 1;
  auto& image_constraints = constraints.image_format_constraints[0];
  image_constraints.pixel_format.type = kIspStreamPixelFormat;
  image_constraints.min_coded_width = kIspStreamMinWidth;
  image_constraints.max_coded_width = kIspStreamMaxWidth;
  image_constraints.min_coded_height = kIspStreamMinHeight;
  image_constraints.max_coded_height = kIspStreamMaxHeight;
  image_constraints.min_bytes_per_row = kIspStreamMinBytesPerRow;
  image_constraints.max_bytes_per_row = kIspStreamMaxBytesPerRow;
  image_constraints.layers = kIspStreamLayers;
  image_constraints.bytes_per_row_divisor = kIspStreamBytesPerRowDivisor;
  image_constraints.color_spaces_count = kIspStreamColorSpaceCount;
  image_constraints.color_space[0].type = kIspStreamColorSpaceType;
  constraints.usage.cpu = fuchsia::sysmem::cpuUsageWrite | fuchsia::sysmem::cpuUsageRead;
  return constraints;
}

static std::vector<fuchsia::sysmem::ImageFormat_2> IspImageFormats() {
  fuchsia::sysmem::ImageFormat_2 ret;
  ret.coded_width = kIspStreamWidth;
  ret.coded_height = kIspStreamHeight;
  ret.pixel_format.type = fuchsia::sysmem::PixelFormatType::NV12;
  std::vector<fuchsia::sysmem::ImageFormat_2> ret_vec;
  ret_vec.push_back(ret);
  return ret_vec;
}

static fuchsia::camera2::StreamProperties StreamProperties(
    fuchsia::camera2::CameraStreamType type) {
  fuchsia::camera2::StreamProperties ret{};
  ret.set_stream_type(type);
  return ret;
}

static fuchsia::camera2::hal::StreamConfig IspStreamConfig() {
  fuchsia::camera2::hal::StreamConfig stream_config;
  stream_config.frame_rate = {
      .frames_per_sec_numerator = kIspStreamFrameRate,
      .frames_per_sec_denominator = 1,
  };
  stream_config.constraints = IspStreamConstraints();
  stream_config.properties = StreamProperties(fuchsia::camera2::CameraStreamType::FULL_RESOLUTION);
  stream_config.image_formats = IspImageFormats();
  return stream_config;
};

fuchsia::camera2::hal::Config DebugConfig() {
  fuchsia::camera2::hal::Config config;
  config.stream_configs.push_back(std::move(IspStreamConfig()));
  return config;
}

}  // namespace camera

#endif  // SRC_CAMERA_DRIVERS_CONTROLLER_CONFIGS_SHERLOCK_ISP_DEBUG_CONFIG_H_
