// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_CAMERA_DRIVERS_CONTROLLER_CONFIGS_SHERLOCK_MONITORING_CONFIG_H_
#define SRC_CAMERA_DRIVERS_CONTROLLER_CONFIGS_SHERLOCK_MONITORING_CONFIG_H_

#include <fuchsia/camera2/hal/cpp/fidl.h>
#include <fuchsia/sysmem/cpp/fidl.h>

#include <vector>

#include "common-util.h"
#include "src/camera/drivers/controller/configs/sherlock/internal-config.h"
#include "src/camera/stream_utils/camera_stream_constraints.h"

// This file contains static information for the Monitor Configuration
// There are three streams in one configuration
// FR --> OutputStreamMLFR (Directly from ISP) (10fps)
// FR --> GDC1 --> OutputStreamMLDS
// DS --> GDC2 --> (GE2D) --> OutputStreamMonitoring
// Not adding GE2D at the moment.

namespace camera {

namespace {

// OutputStreamMLFR Parameters
constexpr uint32_t kOutputStreamMlFRMinBufferForCamping = 5;
constexpr uint32_t kOutputStreamMlFRWidth = 2176;
constexpr uint32_t kOutputStreamMlFRHeight = 2720;
constexpr uint32_t kMaxBytesPerRow = 0xfffffff;
constexpr uint32_t kOutputStreamMlFRLayers = 1;
constexpr uint32_t kISPPerRowDivisor = 128;
constexpr uint32_t kOutputStreamMlFRColorSpaceCount = 1;
constexpr uint32_t kOutputStreamMlFRFrameRate = 10;
constexpr ::fuchsia::sysmem::PixelFormatType kOutputStreamMlFRPixelFormat =
    fuchsia::sysmem::PixelFormatType::NV12;
constexpr ::fuchsia::sysmem::ColorSpaceType kOutputStreamMlFRColorSpaceType =
    fuchsia::sysmem::ColorSpaceType::REC601_PAL;

// OutputStreamMLDS Parameters
constexpr uint32_t kOutputStreamMlDSMinBufferForCamping = 5;
constexpr uint32_t kOutputStreamMlDSWidth = 640;
constexpr uint32_t kOutputStreamMlDSHeight = 512;
constexpr uint32_t kOutputStreamMlDSLayers = 1;
constexpr uint32_t kOutputStreamMlDSColorSpaceCount = 1;
constexpr uint32_t kOutputStreamMlDSFrameRate = 10;
constexpr ::fuchsia::sysmem::PixelFormatType kOutputStreamMlDSPixelFormat =
    fuchsia::sysmem::PixelFormatType::NV12;
constexpr ::fuchsia::sysmem::ColorSpaceType kOutputStreamMlDSColorSpaceType =
    fuchsia::sysmem::ColorSpaceType::REC601_PAL;

// OutputStreamMonitoring Parameters
constexpr uint32_t kOutputStreamDSWidth = 1152;
constexpr uint32_t kOutputStreamDSHeight = 1440;
constexpr uint32_t kOutputStreamMonitoringMinBufferForCamping = 5;
constexpr uint32_t kOutputStreamMonitoringWidth = 1152;
constexpr uint32_t kOutputStreamMonitoringHeight = 864;
constexpr uint32_t kOutputStreamMonitoringLayers = 1;
constexpr uint32_t kOutputStreamMonitoringColorSpaceCount = 1;
constexpr uint32_t kOutputStreamMonitoringWidth1 = 720;
constexpr uint32_t kOutputStreamMonitoringHeight1 = 540;
constexpr uint32_t kOutputStreamMonitoringWidth2 = 512;
constexpr uint32_t kOutputStreamMonitoringHeight2 = 384;
constexpr uint32_t kOutputStreamMonitoringFrameRate = 30;
constexpr ::fuchsia::sysmem::PixelFormatType kOutputStreamMonitoringPixelFormat =
    fuchsia::sysmem::PixelFormatType::NV12;
constexpr ::fuchsia::sysmem::ColorSpaceType kOutputStreamMonitoringColorSpaceType =
    fuchsia::sysmem::ColorSpaceType::REC601_PAL;

constexpr uint32_t kGdcBytesPerRowDivisor = 16;
}  // namespace

/**********************************
 * Output Stream ML FR paramters  *
 **********************************
 */

static std::vector<fuchsia::sysmem::ImageFormat_2> OutputStreamMLFRImageFormats() {
  return {
      {
          .pixel_format = {fuchsia::sysmem::PixelFormatType::NV12},
          .coded_width = kOutputStreamMlFRWidth,
          .coded_height = kOutputStreamMlFRHeight,
          .bytes_per_row = kOutputStreamMlFRWidth,
          .display_width = kOutputStreamMlFRWidth,
          .display_height = kOutputStreamMlFRHeight,
          .color_space.type = kOutputStreamMlFRColorSpaceType,
      },
  };
}

static fuchsia::camera2::hal::StreamConfig OutputStreamMLFRConfig() {
  CameraStreamConstraints stream(fuchsia::camera2::CameraStreamType::FULL_RESOLUTION |
                                 fuchsia::camera2::CameraStreamType::MACHINE_LEARNING);
  stream.AddImageFormat(kOutputStreamMlFRWidth, kOutputStreamMlFRHeight,
                        kOutputStreamMlFRPixelFormat);
  stream.set_bytes_per_row_divisor(kISPPerRowDivisor);
  stream.set_contiguous(true);
  stream.set_frames_per_second(kOutputStreamMlFRFrameRate);
  return stream.ConvertToStreamConfig();
};

/***********************************
 * Output Stream ML DS paramters   *
 ***********************************
 */

static std::vector<fuchsia::sysmem::ImageFormat_2> OutputStreamMLDSImageFormats() {
  return {
      {
          .pixel_format = {fuchsia::sysmem::PixelFormatType::NV12},
          .coded_width = kOutputStreamMlDSWidth,
          .coded_height = kOutputStreamMlDSHeight,
          .bytes_per_row = kOutputStreamMlDSWidth,
          .display_width = kOutputStreamMlDSWidth,
          .display_height = kOutputStreamMlDSHeight,
          .color_space.type = kOutputStreamMlDSColorSpaceType,
      },
  };
}

static fuchsia::camera2::hal::StreamConfig OutputStreamMLDSConfig() {
  CameraStreamConstraints stream(fuchsia::camera2::CameraStreamType::DOWNSCALED_RESOLUTION |
                                 fuchsia::camera2::CameraStreamType::MACHINE_LEARNING);
  stream.AddImageFormat(kOutputStreamMlDSWidth, kOutputStreamMlDSHeight,
                        kOutputStreamMlFRPixelFormat);
  stream.set_bytes_per_row_divisor(kGdcBytesPerRowDivisor);
  stream.set_contiguous(true);
  stream.set_frames_per_second(kOutputStreamMlDSFrameRate);
  return stream.ConvertToStreamConfig();
};

/******************************************
 * Output Stream DS Monitoring paramters  *
 ******************************************
 */

static std::vector<fuchsia::sysmem::ImageFormat_2> MonitorConfigDownScaledResImageFormats() {
  return {
      {
          .pixel_format = {fuchsia::sysmem::PixelFormatType::NV12},
          .coded_width = kOutputStreamDSWidth,
          .coded_height = kOutputStreamDSHeight,
          .bytes_per_row = kOutputStreamDSWidth,
          .display_width = kOutputStreamDSWidth,
          .display_height = kOutputStreamDSHeight,
          .color_space.type = kOutputStreamMlDSColorSpaceType,
      },
  };
}

static std::vector<fuchsia::sysmem::ImageFormat_2> OutputStreamMonitoringImageFormats() {
  return {
      {
          .pixel_format = {fuchsia::sysmem::PixelFormatType::NV12},
          .coded_width = kOutputStreamMonitoringWidth,
          .coded_height = kOutputStreamMonitoringHeight,
          .bytes_per_row = kOutputStreamMonitoringWidth,
          .display_width = kOutputStreamMonitoringWidth,
          .display_height = kOutputStreamMonitoringHeight,
          .color_space.type = kOutputStreamMonitoringColorSpaceType,
      },
      {
          .pixel_format = {fuchsia::sysmem::PixelFormatType::NV12},
          .coded_width = kOutputStreamMonitoringWidth1,
          .coded_height = kOutputStreamMonitoringHeight1,
          .bytes_per_row = kOutputStreamMonitoringWidth1,
          .display_width = kOutputStreamMonitoringWidth1,
          .display_height = kOutputStreamMonitoringHeight1,
          .color_space.type = kOutputStreamMonitoringColorSpaceType,
      },
      {
          .pixel_format = {fuchsia::sysmem::PixelFormatType::NV12},
          .coded_width = kOutputStreamMonitoringWidth2,
          .coded_height = kOutputStreamMonitoringHeight2,
          .bytes_per_row = kOutputStreamMonitoringWidth2,
          .display_width = kOutputStreamMonitoringWidth2,
          .display_height = kOutputStreamMonitoringHeight2,
          .color_space.type = kOutputStreamMonitoringColorSpaceType,
      },
  };
}

static fuchsia::camera2::hal::StreamConfig OutputStreamMonitoringConfig() {
  CameraStreamConstraints stream(fuchsia::camera2::CameraStreamType::MONITORING);
  stream.AddImageFormat(kOutputStreamMonitoringWidth, kOutputStreamMonitoringHeight,
                        kOutputStreamMonitoringPixelFormat);
  stream.AddImageFormat(kOutputStreamMonitoringWidth1, kOutputStreamMonitoringHeight1,
                        kOutputStreamMonitoringPixelFormat);
  stream.AddImageFormat(kOutputStreamMonitoringWidth2, kOutputStreamMonitoringHeight2,
                        kOutputStreamMonitoringPixelFormat);
  stream.set_bytes_per_row_divisor(kGdcBytesPerRowDivisor);
  stream.set_contiguous(true);
  stream.set_frames_per_second(kOutputStreamMonitoringFrameRate);
  return stream.ConvertToStreamConfig();
};

/*****************************
 *  EXTERNAL CONFIGURATIONS  *
 *****************************
 */

fuchsia::camera2::hal::Config MonitoringConfig() {
  fuchsia::camera2::hal::Config config;
  config.stream_configs.push_back(OutputStreamMLFRConfig());
  config.stream_configs.push_back(OutputStreamMLDSConfig());
  config.stream_configs.push_back(OutputStreamMonitoringConfig());
  return config;
}

// ================== INTERNAL CONFIGURATION ======================== //
// FR --> OutputStreamMLFR (Directly from ISP) (10fps)
// FR --> GDC1 --> OutputStreamMLDS (10fps)

static InternalConfigNode OutputStreamMLFR() {
  return {
      .type = kOutputStream,
      .output_frame_rate.frames_per_sec_numerator = kOutputStreamMlFRFrameRate,
      .output_frame_rate.frames_per_sec_denominator = 1,
      .output_stream_type = fuchsia::camera2::CameraStreamType::FULL_RESOLUTION |
                            fuchsia::camera2::CameraStreamType::MACHINE_LEARNING,
  };
}

static InternalConfigNode OutputStreamMLDS() {
  return {
      .type = kOutputStream,
      .output_frame_rate.frames_per_sec_numerator = kOutputStreamMlDSFrameRate,
      .output_frame_rate.frames_per_sec_denominator = 1,
      .output_stream_type = fuchsia::camera2::CameraStreamType::DOWNSCALED_RESOLUTION |
                            fuchsia::camera2::CameraStreamType::MACHINE_LEARNING,
  };
}

fuchsia::sysmem::BufferCollectionConstraints Gdc1Constraints() {
  fuchsia::sysmem::BufferCollectionConstraints constraints;
  constraints.has_buffer_memory_constraints = true;
  constraints.buffer_memory_constraints.physically_contiguous_required = true;
  constraints.image_format_constraints_count = 1;
  auto& image_constraints = constraints.image_format_constraints[0];
  image_constraints.pixel_format.type = fuchsia::sysmem::PixelFormatType::NV12;
  image_constraints.min_coded_width = kOutputStreamMlFRWidth;
  image_constraints.max_coded_width = kOutputStreamMlFRWidth;
  image_constraints.min_coded_height = kOutputStreamMlFRHeight;
  image_constraints.max_coded_height = kOutputStreamMlFRHeight;
  image_constraints.min_bytes_per_row = kOutputStreamMlFRWidth;
  image_constraints.max_bytes_per_row = kOutputStreamMlFRWidth;
  image_constraints.layers = 1;
  image_constraints.bytes_per_row_divisor = kGdcBytesPerRowDivisor;
  image_constraints.color_spaces_count = 1;
  image_constraints.color_space[0].type = fuchsia::sysmem::ColorSpaceType::REC601_PAL;
  constraints.usage.cpu = fuchsia::sysmem::cpuUsageWrite | fuchsia::sysmem::cpuUsageRead;
  return constraints;
}

static InternalConfigNode Gdc1() {
  return {
      .type = kGdc,
      .output_frame_rate.frames_per_sec_numerator = kOutputStreamMlDSFrameRate,
      .output_frame_rate.frames_per_sec_denominator = 1,
      .output_stream_type = fuchsia::camera2::CameraStreamType::DOWNSCALED_RESOLUTION |
                            fuchsia::camera2::CameraStreamType::MACHINE_LEARNING,
      .child_nodes =
          {
              {
                  OutputStreamMLDS(),
              },
          },
      .gdc_info.config_type =
          {
              GdcConfig::MONITORING_ML,
          },
      .constraints = Gdc1Constraints(),
      .image_formats = OutputStreamMLDSImageFormats(),
  };
}

fuchsia::sysmem::BufferCollectionConstraints MonitorConfigFullResConstraints() {
  fuchsia::sysmem::BufferCollectionConstraints constraints;
  constraints.min_buffer_count_for_camping = kOutputStreamMlFRMinBufferForCamping;
  constraints.has_buffer_memory_constraints = true;
  constraints.buffer_memory_constraints.physically_contiguous_required = true;
  constraints.image_format_constraints_count = 1;
  auto& image_constraints = constraints.image_format_constraints[0];
  image_constraints.pixel_format.type = fuchsia::sysmem::PixelFormatType::NV12;
  image_constraints.required_min_coded_width = kOutputStreamMlFRWidth;
  image_constraints.max_coded_width = kOutputStreamMlFRWidth;
  image_constraints.required_min_coded_height = kOutputStreamMlFRHeight;
  image_constraints.max_coded_height = kOutputStreamMlFRHeight;
  image_constraints.min_bytes_per_row = kOutputStreamMlFRWidth;
  image_constraints.max_bytes_per_row = kOutputStreamMlFRWidth;
  image_constraints.layers = 1;
  image_constraints.bytes_per_row_divisor = kISPPerRowDivisor;
  image_constraints.color_spaces_count = 1;
  image_constraints.color_space[0].type = fuchsia::sysmem::ColorSpaceType::REC601_PAL;
  constraints.usage.cpu = fuchsia::sysmem::cpuUsageWrite | fuchsia::sysmem::cpuUsageRead;
  return constraints;
}

InternalConfigNode MonitorConfigFullRes() {
  return {
      .type = kInputStream,
      .output_frame_rate.frames_per_sec_numerator = kOutputStreamMonitoringFrameRate,
      .output_frame_rate.frames_per_sec_denominator = 1,
      .input_stream_type = fuchsia::camera2::CameraStreamType::FULL_RESOLUTION,
      .supported_streams =
          {
              fuchsia::camera2::CameraStreamType::FULL_RESOLUTION |
                  fuchsia::camera2::CameraStreamType::MACHINE_LEARNING,
              fuchsia::camera2::CameraStreamType::DOWNSCALED_RESOLUTION |
                  fuchsia::camera2::CameraStreamType::MACHINE_LEARNING,
          },
      .child_nodes =
          {
              {
                  OutputStreamMLFR(),
              },
              {
                  Gdc1(),
              },
          },
      .constraints = MonitorConfigFullResConstraints(),
      .image_formats = OutputStreamMLFRImageFormats(),
  };
}

// DS --> GDC2 --> GE2D --> OutputStreamMonitoring

static InternalConfigNode OutputStreamMonitoring() {
  return {
      .type = kOutputStream,
      .output_frame_rate.frames_per_sec_numerator = kOutputStreamMonitoringFrameRate,
      .output_frame_rate.frames_per_sec_denominator = 1,
      .output_stream_type = fuchsia::camera2::CameraStreamType::VIDEO_CONFERENCE,
  };
}

fuchsia::sysmem::BufferCollectionConstraints Gdc2Constraints() {
  fuchsia::sysmem::BufferCollectionConstraints constraints;
  constraints.has_buffer_memory_constraints = true;
  constraints.buffer_memory_constraints.physically_contiguous_required = true;
  constraints.image_format_constraints_count = 1;
  auto& image_constraints = constraints.image_format_constraints[0];
  image_constraints.pixel_format.type = fuchsia::sysmem::PixelFormatType::NV12;
  image_constraints.min_coded_width = kOutputStreamDSWidth;
  image_constraints.max_coded_width = kOutputStreamDSWidth;
  image_constraints.min_coded_height = kOutputStreamDSHeight;
  image_constraints.max_coded_height = kOutputStreamDSHeight;
  image_constraints.min_bytes_per_row = kOutputStreamDSWidth;
  image_constraints.max_bytes_per_row = kOutputStreamDSWidth;
  image_constraints.layers = 1;
  image_constraints.bytes_per_row_divisor = kGdcBytesPerRowDivisor;
  image_constraints.color_spaces_count = 1;
  image_constraints.color_space[0].type = fuchsia::sysmem::ColorSpaceType::REC601_PAL;
  constraints.usage.cpu = fuchsia::sysmem::cpuUsageWrite | fuchsia::sysmem::cpuUsageRead;
  return constraints;
}

static InternalConfigNode Gdc2() {
  return {
      .type = kGdc,
      .output_frame_rate.frames_per_sec_numerator = kOutputStreamMonitoringFrameRate,
      .output_frame_rate.frames_per_sec_denominator = 1,
      .output_stream_type = fuchsia::camera2::CameraStreamType::MONITORING,
      .child_nodes =
          {
              {
                  OutputStreamMonitoring(),
              },
          },
      .gdc_info.config_type =
          {
              GdcConfig::MONITORING_360p,
              GdcConfig::MONITORING_480p,
              GdcConfig::MONITORING_720p,
          },
      .constraints = Gdc2Constraints(),
      .image_formats = OutputStreamMonitoringImageFormats(),
  };
}

fuchsia::sysmem::BufferCollectionConstraints MonitorConfigDownScaledResConstraints() {
  fuchsia::sysmem::BufferCollectionConstraints constraints;
  constraints.min_buffer_count_for_camping = kOutputStreamMonitoringMinBufferForCamping;
  constraints.has_buffer_memory_constraints = true;
  constraints.buffer_memory_constraints.physically_contiguous_required = true;
  constraints.image_format_constraints_count = 1;
  auto& image_constraints = constraints.image_format_constraints[0];
  image_constraints.pixel_format.type = fuchsia::sysmem::PixelFormatType::NV12;
  image_constraints.required_min_coded_width = kOutputStreamDSWidth;
  image_constraints.max_coded_width = kOutputStreamDSWidth;
  image_constraints.required_min_coded_height = kOutputStreamDSHeight;
  image_constraints.max_coded_height = kOutputStreamDSHeight;
  image_constraints.min_bytes_per_row = kOutputStreamDSWidth;
  image_constraints.max_bytes_per_row = kOutputStreamDSWidth;
  image_constraints.layers = 1;
  image_constraints.bytes_per_row_divisor = kISPPerRowDivisor;
  image_constraints.color_spaces_count = 1;
  image_constraints.color_space[0].type = fuchsia::sysmem::ColorSpaceType::REC601_PAL;
  constraints.usage.cpu = fuchsia::sysmem::cpuUsageWrite | fuchsia::sysmem::cpuUsageRead;
  return constraints;
}

InternalConfigNode MonitorConfigDownScaledRes() {
  return {
      .type = kInputStream,
      .output_frame_rate.frames_per_sec_numerator = kOutputStreamMonitoringFrameRate,
      .output_frame_rate.frames_per_sec_denominator = 1,
      .input_stream_type = fuchsia::camera2::CameraStreamType::DOWNSCALED_RESOLUTION,
      .supported_streams =
          {
              fuchsia::camera2::CameraStreamType::MONITORING,
          },
      .child_nodes =
          {
              {
                  Gdc2(),
              },
          },
      .constraints = MonitorConfigDownScaledResConstraints(),
      .image_formats = MonitorConfigDownScaledResImageFormats(),
  };
}

}  // namespace camera

#endif  // SRC_CAMERA_DRIVERS_CONTROLLER_CONFIGS_SHERLOCK_MONITORING_CONFIG_H_
