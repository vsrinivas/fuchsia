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
#include "src/camera/stream_utils/stream_constraints.h"

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
constexpr uint32_t kOutputStreamMlFRLayers = 1;
constexpr uint32_t kISPPerRowDivisor = 128;
constexpr uint32_t kOutputStreamMlFRFrameRate = 10;
constexpr ::fuchsia::sysmem::PixelFormatType kOutputStreamMlFRPixelFormat =
    fuchsia::sysmem::PixelFormatType::NV12;

// OutputStreamMLDS Parameters
constexpr uint32_t kOutputStreamMlDSMinBufferForCamping = 5;
constexpr uint32_t kOutputStreamMlDSWidth = 640;
constexpr uint32_t kOutputStreamMlDSHeight = 512;
constexpr uint32_t kOutputStreamMlDSLayers = 1;
constexpr uint32_t kOutputStreamMlDSFrameRate = 10;
constexpr ::fuchsia::sysmem::PixelFormatType kOutputStreamMlDSPixelFormat =
    fuchsia::sysmem::PixelFormatType::NV12;

// OutputStreamMonitoring Parameters
constexpr uint32_t kOutputStreamDSWidth = 1152;
constexpr uint32_t kOutputStreamDSHeight = 1440;
constexpr uint32_t kOutputStreamMonitoringMinBufferForCamping = 5;
constexpr uint32_t kOutputStreamMonitoringWidth = 1152;
constexpr uint32_t kOutputStreamMonitoringHeight = 864;
constexpr uint32_t kOutputStreamMonitoringWidth1 = 720;
constexpr uint32_t kOutputStreamMonitoringHeight1 = 540;
constexpr uint32_t kOutputStreamMonitoringWidth2 = 512;
constexpr uint32_t kOutputStreamMonitoringHeight2 = 384;
constexpr uint32_t kOutputStreamMonitoringFrameRate = 30;
constexpr ::fuchsia::sysmem::PixelFormatType kOutputStreamMonitoringPixelFormat =
    fuchsia::sysmem::PixelFormatType::NV12;

constexpr uint32_t kGdcBytesPerRowDivisor = 16;
}  // namespace

/**********************************
 * Output Stream ML FR paramters  *
 **********************************
 */

static std::vector<fuchsia::sysmem::ImageFormat_2> OutputStreamMLFRImageFormats() {
  return {
      StreamConstraints::MakeImageFormat(kOutputStreamMlFRWidth, kOutputStreamMlFRHeight,
                                         kOutputStreamMlFRPixelFormat),
  };
}

static fuchsia::camera2::hal::StreamConfig OutputStreamMLFRConfig() {
  StreamConstraints stream(fuchsia::camera2::CameraStreamType::FULL_RESOLUTION |
                           fuchsia::camera2::CameraStreamType::MACHINE_LEARNING);
  stream.AddImageFormat(kOutputStreamMlFRWidth, kOutputStreamMlFRHeight,
                        kOutputStreamMlFRPixelFormat);
  stream.set_bytes_per_row_divisor(kISPPerRowDivisor);
  stream.set_contiguous(true);
  stream.set_frames_per_second(kOutputStreamMlFRFrameRate);
  stream.set_buffer_count_for_camping(kOutputStreamMlFRMinBufferForCamping);
  return stream.ConvertToStreamConfig();
}

/***********************************
 * Output Stream ML DS paramters   *
 ***********************************
 */

static std::vector<fuchsia::sysmem::ImageFormat_2> OutputStreamMLDSImageFormats() {
  return {
      StreamConstraints::MakeImageFormat(kOutputStreamMlDSWidth, kOutputStreamMlDSHeight,
                                         kOutputStreamMlDSPixelFormat),
  };
}

static fuchsia::camera2::hal::StreamConfig OutputStreamMLDSConfig() {
  StreamConstraints stream(fuchsia::camera2::CameraStreamType::DOWNSCALED_RESOLUTION |
                           fuchsia::camera2::CameraStreamType::MACHINE_LEARNING);
  stream.AddImageFormat(kOutputStreamMlDSWidth, kOutputStreamMlDSHeight,
                        kOutputStreamMlFRPixelFormat);
  stream.set_bytes_per_row_divisor(kGdcBytesPerRowDivisor);
  stream.set_contiguous(true);
  stream.set_frames_per_second(kOutputStreamMlDSFrameRate);
  stream.set_buffer_count_for_camping(kOutputStreamMlDSMinBufferForCamping);
  return stream.ConvertToStreamConfig();
}

/******************************************
 * Output Stream DS Monitoring paramters  *
 ******************************************
 */

static std::vector<fuchsia::sysmem::ImageFormat_2> MonitorConfigDownScaledResImageFormats() {
  return {
      StreamConstraints::MakeImageFormat(kOutputStreamDSWidth, kOutputStreamDSHeight,
                                         kOutputStreamMonitoringPixelFormat),
  };
}

static std::vector<fuchsia::sysmem::ImageFormat_2> OutputStreamMonitoringImageFormats() {
  return {
      StreamConstraints::MakeImageFormat(kOutputStreamMonitoringWidth,
                                         kOutputStreamMonitoringHeight,
                                         kOutputStreamMonitoringPixelFormat),
      StreamConstraints::MakeImageFormat(kOutputStreamMonitoringWidth1,
                                         kOutputStreamMonitoringHeight1,
                                         kOutputStreamMonitoringPixelFormat),
      StreamConstraints::MakeImageFormat(kOutputStreamMonitoringWidth2,
                                         kOutputStreamMonitoringHeight2,
                                         kOutputStreamMonitoringPixelFormat),
  };
}

static fuchsia::camera2::hal::StreamConfig OutputStreamMonitoringConfig() {
  StreamConstraints stream(fuchsia::camera2::CameraStreamType::MONITORING);
  stream.AddImageFormat(kOutputStreamMonitoringWidth, kOutputStreamMonitoringHeight,
                        kOutputStreamMonitoringPixelFormat);
  stream.AddImageFormat(kOutputStreamMonitoringWidth1, kOutputStreamMonitoringHeight1,
                        kOutputStreamMonitoringPixelFormat);
  stream.AddImageFormat(kOutputStreamMonitoringWidth2, kOutputStreamMonitoringHeight2,
                        kOutputStreamMonitoringPixelFormat);
  stream.set_bytes_per_row_divisor(kGdcBytesPerRowDivisor);
  stream.set_contiguous(true);
  stream.set_frames_per_second(kOutputStreamMonitoringFrameRate);
  stream.set_buffer_count_for_camping(kOutputStreamMonitoringMinBufferForCamping);
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
      .supported_streams =
          {
              fuchsia::camera2::CameraStreamType::FULL_RESOLUTION |
                  fuchsia::camera2::CameraStreamType::MACHINE_LEARNING,
          },
  };
}

static InternalConfigNode OutputStreamMLDS() {
  return {
      .type = kOutputStream,
      .output_frame_rate.frames_per_sec_numerator = kOutputStreamMlDSFrameRate,
      .output_frame_rate.frames_per_sec_denominator = 1,
      .supported_streams =
          {
              fuchsia::camera2::CameraStreamType::DOWNSCALED_RESOLUTION |
                  fuchsia::camera2::CameraStreamType::MACHINE_LEARNING,
          },
  };
}

fuchsia::sysmem::BufferCollectionConstraints Gdc1Constraints() {
  StreamConstraints stream_constraints;
  stream_constraints.set_bytes_per_row_divisor(kGdcBytesPerRowDivisor);
  stream_constraints.set_contiguous(true);
  stream_constraints.AddImageFormat(kOutputStreamMlFRWidth, kOutputStreamMlFRHeight,
                                    kOutputStreamMlFRPixelFormat);
  stream_constraints.set_buffer_count_for_camping(0);
  return stream_constraints.MakeBufferCollectionConstraints();
}

static InternalConfigNode Gdc1() {
  return {
      .type = kGdc,
      .output_frame_rate.frames_per_sec_numerator = kOutputStreamMlDSFrameRate,
      .output_frame_rate.frames_per_sec_denominator = 1,
      .supported_streams =
          {
              fuchsia::camera2::CameraStreamType::DOWNSCALED_RESOLUTION |
                  fuchsia::camera2::CameraStreamType::MACHINE_LEARNING,
          },
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
      .input_constraints = Gdc1Constraints(),
      // This node doesn't need |output_constraints| because next node is Output node so
      // there is no need to create internal buffers.
      .output_constraints = InvalidConstraints(),
      .image_formats = OutputStreamMLDSImageFormats(),
  };
}

fuchsia::sysmem::BufferCollectionConstraints MonitorConfigFullResConstraints() {
  StreamConstraints stream_constraints;
  stream_constraints.set_bytes_per_row_divisor(kISPPerRowDivisor);
  stream_constraints.set_contiguous(true);
  stream_constraints.AddImageFormat(kOutputStreamMlFRWidth, kOutputStreamMlFRHeight,
                                    kOutputStreamMlFRPixelFormat);
  stream_constraints.set_buffer_count_for_camping(kOutputStreamMlFRMinBufferForCamping);
  return stream_constraints.MakeBufferCollectionConstraints();
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
      // Input node doesn't need |input_constraints|
      .input_constraints = InvalidConstraints(),
      .output_constraints = MonitorConfigFullResConstraints(),
      .image_formats = OutputStreamMLFRImageFormats(),
  };
}

// DS --> GDC2 --> GE2D --> OutputStreamMonitoring

static InternalConfigNode OutputStreamMonitoring() {
  return {
      .type = kOutputStream,
      .output_frame_rate.frames_per_sec_numerator = kOutputStreamMonitoringFrameRate,
      .output_frame_rate.frames_per_sec_denominator = 1,
      .supported_streams =
          {
              fuchsia::camera2::CameraStreamType::MONITORING,
          },
  };
}

fuchsia::sysmem::BufferCollectionConstraints Gdc2Constraints() {
  StreamConstraints stream_constraints;
  stream_constraints.set_bytes_per_row_divisor(kGdcBytesPerRowDivisor);
  stream_constraints.set_contiguous(true);
  stream_constraints.AddImageFormat(kOutputStreamDSWidth, kOutputStreamDSHeight,
                                    kOutputStreamMonitoringPixelFormat);
  stream_constraints.set_buffer_count_for_camping(0);
  return stream_constraints.MakeBufferCollectionConstraints();
}

static InternalConfigNode Gdc2() {
  return {
      .type = kGdc,
      .output_frame_rate.frames_per_sec_numerator = kOutputStreamMonitoringFrameRate,
      .output_frame_rate.frames_per_sec_denominator = 1,
      .supported_streams =
          {
              fuchsia::camera2::CameraStreamType::MONITORING,
          },
      .child_nodes =
          {
              {
                  OutputStreamMonitoring(),
              },
          },
      .gdc_info.config_type =
          {
              GdcConfig::MONITORING_720p,
              GdcConfig::MONITORING_480p,
              GdcConfig::MONITORING_360p,
          },
      .input_constraints = Gdc2Constraints(),
      // This node does need |output_constraints| when we add GE2D node.
      .output_constraints = InvalidConstraints(),
      .image_formats = OutputStreamMonitoringImageFormats(),
  };
}

fuchsia::sysmem::BufferCollectionConstraints MonitorConfigDownScaledResConstraints() {
  StreamConstraints stream_constraints;
  stream_constraints.set_bytes_per_row_divisor(kISPPerRowDivisor);
  stream_constraints.set_contiguous(true);
  stream_constraints.AddImageFormat(kOutputStreamDSWidth, kOutputStreamDSHeight,
                                    kOutputStreamMonitoringPixelFormat);
  stream_constraints.set_buffer_count_for_camping(kOutputStreamMlDSMinBufferForCamping);
  return stream_constraints.MakeBufferCollectionConstraints();
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
      .input_constraints = InvalidConstraints(),
      .output_constraints = MonitorConfigDownScaledResConstraints(),
      // Input node doesn't need |input_constraints|
      .image_formats = MonitorConfigDownScaledResImageFormats(),
  };
}

}  // namespace camera

#endif  // SRC_CAMERA_DRIVERS_CONTROLLER_CONFIGS_SHERLOCK_MONITORING_CONFIG_H_
