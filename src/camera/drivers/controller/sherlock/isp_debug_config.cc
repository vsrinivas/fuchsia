// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/camera/drivers/controller/sherlock/isp_debug_config.h"

// This file contains static information for the ISP Debug Configuration
// There are three streams in one configuration
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

static std::vector<fuchsia::sysmem::ImageFormat_2> DSDebugStreamImageFormats() {
  return {
      camera::StreamConstraints::MakeImageFormat(kDSStreamWidth, kDSStreamHeight,
                                                 kStreamPixelFormat),
  };
}

static fuchsia::camera2::hal::StreamConfig DSDebugStreamConfig() {
  StreamConstraints stream(fuchsia::camera2::CameraStreamType::DOWNSCALED_RESOLUTION);
  stream.AddImageFormat(kDSStreamWidth, kDSStreamHeight, kStreamPixelFormat);
  stream.set_bytes_per_row_divisor(kIspBytesPerRowDivisor);
  stream.set_contiguous(true);
  stream.set_frames_per_second(kDSStreamFrameRate);
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
  config.stream_configs.push_back(DSDebugStreamConfig());
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

static InternalConfigNode OutputDSStream() {
  return {
      .type = kOutputStream,
      .output_frame_rate.frames_per_sec_numerator = kDSStreamFrameRate,
      .output_frame_rate.frames_per_sec_denominator = 1,
      .supported_streams =
          {
              {
                  .type = fuchsia::camera2::CameraStreamType::DOWNSCALED_RESOLUTION,
                  .supports_dynamic_resolution = false,
                  .supports_crop_region = false,
              },
          },
      .image_formats = DSDebugStreamImageFormats(),
  };
}

InternalConfigNode DebugConfigDownScaledRes() {
  return {
      .type = kInputStream,
      // For node type |kInputStream| we will be ignoring the
      // frame rate divisor.
      .output_frame_rate.frames_per_sec_numerator = kFRStreamFrameRate,
      .output_frame_rate.frames_per_sec_denominator = 1,
      .input_stream_type = fuchsia::camera2::CameraStreamType::DOWNSCALED_RESOLUTION,
      .supported_streams =
          {
              {
                  .type = fuchsia::camera2::CameraStreamType::DOWNSCALED_RESOLUTION,
                  .supports_dynamic_resolution = false,
                  .supports_crop_region = false,
              },
          },
      .child_nodes =
          {
              {
                  OutputDSStream(),
              },
          },
      .image_formats = DSDebugStreamImageFormats(),
  };
}

}  // namespace camera
