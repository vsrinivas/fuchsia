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
#include "src/camera/stream_utils/camera_stream_constraints.h"

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

static std::vector<fuchsia::sysmem::ImageFormat_2> IspDebugStreamImageFormats() {
  return {
      {
          .pixel_format = {fuchsia::sysmem::PixelFormatType::NV12},
          .coded_width = kIspStreamWidth,
          .coded_height = kIspStreamHeight,
          .bytes_per_row = kIspStreamStride,
          .display_width = kIspStreamWidth,
          .display_height = kIspStreamHeight,
          .color_space.type = kIspStreamColorSpaceType,
      },
  };
}

static fuchsia::camera2::hal::StreamConfig IspDebugStreamConfig() {
  CameraStreamConstraints stream(fuchsia::camera2::CameraStreamType::FULL_RESOLUTION);
  stream.AddImageFormat(kIspStreamWidth, kIspStreamHeight, kIspStreamPixelFormat);
  stream.set_bytes_per_row_divisor(kIspStreamBytesPerRowDivisor);
  stream.set_contiguous(true);
  stream.set_frames_per_second(kIspStreamFrameRate);
  stream.set_buffer_count_for_camping(kIspStreamMinBufferForCamping);
  return stream.ConvertToStreamConfig();
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
  return {
      .type = kOutputStream,
      .output_frame_rate.frames_per_sec_numerator = kIspStreamFrameRate,
      .output_frame_rate.frames_per_sec_denominator = 1,
      .output_stream_type = fuchsia::camera2::CameraStreamType::FULL_RESOLUTION,
  };
}

InternalConfigNode DebugConfigFullRes() {
  return {
      .type = kInputStream,
      // For node type |kInputStream| we will be ignoring the
      // frame rate divisor.
      .output_frame_rate.frames_per_sec_numerator = kIspStreamFrameRate,
      .output_frame_rate.frames_per_sec_denominator = 1,
      .input_stream_type = fuchsia::camera2::CameraStreamType::FULL_RESOLUTION,
      .supported_streams =
          {
              fuchsia::camera2::CameraStreamType::FULL_RESOLUTION,
          },
      .child_nodes =
          {
              {
                  OutputStream(),
              },
          },
      .image_formats = IspDebugStreamImageFormats(),
  };
}

}  // namespace camera

#endif  // SRC_CAMERA_DRIVERS_CONTROLLER_CONFIGS_SHERLOCK_ISP_DEBUG_CONFIG_H_
