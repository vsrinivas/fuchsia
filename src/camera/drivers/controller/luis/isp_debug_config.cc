// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/camera/drivers/controller/luis/isp_debug_config.h"

#include <fuchsia/camera2/hal/cpp/fidl.h>
#include <fuchsia/sysmem/cpp/fidl.h>

#include "src/camera/drivers/controller/luis/common_util.h"
#include "src/camera/lib/stream_utils/stream_constraints.h"

// This file contains static information for the ISP Debug Configuration
// There is one stream in one configuration
// FR --> OutputStreamML (Directly from ISP)

namespace camera {

/*****************************
 * Output Stream paramters*
 *****************************
 */

static std::vector<fuchsia::sysmem::ImageFormat_2> FRDebugStreamImageFormats() {
  return {
      camera::StreamConstraints::MakeImageFormat(kFRStreamWidth, kFRStreamHeight,
                                                 kStreamPixelFormat),
  };
}

static fuchsia::camera2::hal::StreamConfig FRDebugStreamConfig() {
  StreamConstraints stream(fuchsia::camera2::CameraStreamType::FULL_RESOLUTION);
  stream.AddImageFormat(kFRStreamWidth, kFRStreamHeight, kStreamPixelFormat);
  stream.set_bytes_per_row_divisor(kIspBytesPerRowDivisor);
  stream.set_contiguous(true);
  stream.set_frames_per_second(kFRStreamFrameRate);
  stream.set_buffer_count_for_camping(kStreamMinBufferForCamping);
  return stream.ConvertToStreamConfig();
};

/*****************************
 *  EXTERNAL CONFIGURATIONS  *
 *****************************
 */

fuchsia::camera2::hal::Config DebugConfig() {
  fuchsia::camera2::hal::Config config;
  config.stream_configs.push_back(FRDebugStreamConfig());
  return config;
}

/*****************************
 *  INTERNAL CONFIGURATIONS  *
 *****************************
 */

static InternalConfigNode OutputFRStream() {
  return {
      .type = kOutputStream,
      .output_frame_rate.frames_per_sec_numerator = kFRStreamFrameRate,
      .output_frame_rate.frames_per_sec_denominator = 1,
      .supported_streams =
          {
              {
                  .type = fuchsia::camera2::CameraStreamType::FULL_RESOLUTION,
                  .supports_dynamic_resolution = false,
                  .supports_crop_region = false,
              },
          },
      .image_formats = FRDebugStreamImageFormats(),
  };
}

InternalConfigNode DebugConfigFullRes() {
  return {
      .type = kInputStream,
      // For node type |kInputStream| we will be ignoring the
      // frame rate divisor.
      .output_frame_rate.frames_per_sec_numerator = kFRStreamFrameRate,
      .output_frame_rate.frames_per_sec_denominator = 1,
      .input_stream_type = fuchsia::camera2::CameraStreamType::FULL_RESOLUTION,
      .supported_streams =
          {
              {
                  .type = fuchsia::camera2::CameraStreamType::FULL_RESOLUTION,
                  .supports_dynamic_resolution = false,
                  .supports_crop_region = false,
              },
          },
      .child_nodes =
          {
              {
                  OutputFRStream(),
              },
          },
      .image_formats = FRDebugStreamImageFormats(),
  };
}

}  // namespace camera
