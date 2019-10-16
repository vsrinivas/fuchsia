// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_CAMERA_DRIVERS_CONTROLLER_CONFIGS_SHERLOCK_ISP_DEBUG_CONFIG_H_
#define SRC_CAMERA_DRIVERS_CONTROLLER_CONFIGS_SHERLOCK_ISP_DEBUG_CONFIG_H_

#include <fuchsia/camera2/hal/cpp/fidl.h>
#include <fuchsia/sysmem/cpp/fidl.h>

#include <vector>

#include "common-util.h"
#include "src/camera/drivers/controller/configs/sherlock/internal-config.h"

// This file contains static information for the ISP Debug Configuration
// There are three streams in one configuration
// FR --> OutputStreamML (Directly from ISP)

namespace camera {

namespace {

// IspDebugStream Parameters
constexpr uint32_t kIspStreamMinBufferForCamping = 5;
constexpr uint32_t kIspStreamWidth = 2176;
constexpr uint32_t kIspStreamHeight = 2720;
constexpr uint32_t kIspStreamStride = 2176;
constexpr uint32_t kIspStreamLayers = 1;
constexpr uint32_t kIspStreamBytesPerRowDivisor = 128;
constexpr uint32_t kIspStreamColorSpaceCount = 1;
constexpr uint32_t kIspStreamFrameRate = 30;
constexpr ::fuchsia::sysmem::PixelFormatType kIspStreamPixelFormat =
    fuchsia::sysmem::PixelFormatType::NV12;
constexpr ::fuchsia::sysmem::ColorSpaceType kIspStreamColorSpaceType =
    fuchsia::sysmem::ColorSpaceType::REC601_PAL;

}  // namespace

/*****************************
 * Output Stream ML paramters*
 *****************************
 */

static constexpr fuchsia::sysmem::BufferCollectionConstraints IspDebugStreamConstraints() {
  fuchsia::sysmem::BufferCollectionConstraints constraints;
  constraints.min_buffer_count_for_camping = kIspStreamMinBufferForCamping;
  constraints.has_buffer_memory_constraints = true;
  constraints.buffer_memory_constraints.physically_contiguous_required = true;
  constraints.image_format_constraints_count = 1;
  constraints.has_buffer_memory_constraints = true;
  constraints.buffer_memory_constraints.physically_contiguous_required = true;
  auto& image_constraints = constraints.image_format_constraints[0];
  image_constraints.pixel_format.type = kIspStreamPixelFormat;
  image_constraints.min_coded_width = kIspStreamWidth;
  image_constraints.max_coded_width = kIspStreamWidth;
  image_constraints.min_coded_height = kIspStreamHeight;
  image_constraints.max_coded_height = kIspStreamHeight;
  image_constraints.min_bytes_per_row = kIspStreamStride;
  image_constraints.max_bytes_per_row = kIspStreamStride;
  image_constraints.layers = kIspStreamLayers;
  image_constraints.bytes_per_row_divisor = kIspStreamBytesPerRowDivisor;
  image_constraints.color_spaces_count = kIspStreamColorSpaceCount;
  image_constraints.color_space[0].type = kIspStreamColorSpaceType;
  constraints.usage.cpu = fuchsia::sysmem::cpuUsageWrite | fuchsia::sysmem::cpuUsageRead;
  return constraints;
}

static std::vector<fuchsia::sysmem::ImageFormat_2> IspDebugStreamImageFormats() {
  fuchsia::sysmem::ImageFormat_2 format{
      .pixel_format = {fuchsia::sysmem::PixelFormatType::NV12},
      .coded_width = kIspStreamWidth,
      .coded_height = kIspStreamHeight,
      .bytes_per_row = kIspStreamStride,
  };
  std::vector<fuchsia::sysmem::ImageFormat_2> ret_vec;
  ret_vec.push_back(format);
  return ret_vec;
}

static fuchsia::camera2::hal::StreamConfig IspDebugStreamConfig() {
  fuchsia::camera2::hal::StreamConfig stream_config;
  stream_config.frame_rate = {
      .frames_per_sec_numerator = kIspStreamFrameRate,
      .frames_per_sec_denominator = 1,
  };
  stream_config.constraints = IspDebugStreamConstraints();
  stream_config.properties =
      GetStreamProperties(fuchsia::camera2::CameraStreamType::FULL_RESOLUTION);
  stream_config.image_formats = IspDebugStreamImageFormats();
  return stream_config;
};

/*****************************
 *  EXTERNAL CONFIGURATIONS  *
 *****************************
 */

fuchsia::camera2::hal::Config DebugConfig() {
  fuchsia::camera2::hal::Config config;
  config.stream_configs.push_back(IspDebugStreamConfig());
  return config;
}

/*****************************
 *  INTERNAL CONFIGURATIONS  *
 *****************************
 */

// FR --> OutputStream

static InternalConfigNode OutputStream() {
  InternalConfigNode node;
  node.type = kOutputStream;
  node.output_frame_rate.frames_per_sec_numerator = kIspStreamFrameRate;
  node.output_frame_rate.frames_per_sec_denominator = 1;
  node.output_stream_type = fuchsia::camera2::CameraStreamType::FULL_RESOLUTION;
  return node;
}

InternalConfigNode DebugConfigFullRes() {
  InternalConfigNode node;
  node.type = kInputStream;
  // For node type |kInputStream| we will be ignoring the
  // frame rate divisor.
  node.output_frame_rate.frames_per_sec_numerator = kIspStreamFrameRate;
  node.output_frame_rate.frames_per_sec_denominator = 1;
  node.input_stream_type = fuchsia::camera2::CameraStreamType::FULL_RESOLUTION;
  node.supported_streams.push_back(fuchsia::camera2::CameraStreamType::FULL_RESOLUTION);
  node.child_nodes.push_back(OutputStream());
  return node;
}

}  // namespace camera

#endif  // SRC_CAMERA_DRIVERS_CONTROLLER_CONFIGS_SHERLOCK_ISP_DEBUG_CONFIG_H_
