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
constexpr uint32_t kOutputStreamMlFRMaxBytesPerRow = 0xfffffff;
constexpr uint32_t kOutputStreamMlFRLayers = 1;
constexpr uint32_t kOutputStreamMlFRBytesPerRowDivisor = 128;
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
constexpr uint32_t kOutputStreamMlDSMaxBytesPerRow = 0xfffffff;
constexpr uint32_t kOutputStreamMlDSLayers = 1;
constexpr uint32_t kOutputStreamMlDSBytesPerRowDivisor = 128;
constexpr uint32_t kOutputStreamMlDSColorSpaceCount = 1;
constexpr uint32_t kOutputStreamMlDSFrameRate = 10;
constexpr ::fuchsia::sysmem::PixelFormatType kOutputStreamMlDSPixelFormat =
    fuchsia::sysmem::PixelFormatType::NV12;
constexpr ::fuchsia::sysmem::ColorSpaceType kOutputStreamMlDSColorSpaceType =
    fuchsia::sysmem::ColorSpaceType::REC601_PAL;

// OutputStreamMonitoring Parameters
constexpr uint32_t kOutputStreamMonitoringMinBufferForCamping = 5;
constexpr uint32_t kOutputStreamMonitoringWidth = 1152;
constexpr uint32_t kOutputStreamMonitoringHeight = 864;
constexpr uint32_t kOutputStreamMonitoringMaxBytesPerRow = 0xfffffff;
constexpr uint32_t kOutputStreamMonitoringLayers = 1;
constexpr uint32_t kOutputStreamMonitoringBytesPerRowDivisor = 128;
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

}  // namespace

/**********************************
 * Output Stream ML FR paramters  *
 **********************************
 */

static constexpr fuchsia::sysmem::BufferCollectionConstraints OutputStreamMLFRConstraints() {
  fuchsia::sysmem::BufferCollectionConstraints constraints;
  constraints.min_buffer_count_for_camping = kOutputStreamMlFRMinBufferForCamping;
  constraints.image_format_constraints_count = 1;
  auto& image_constraints = constraints.image_format_constraints[0];
  image_constraints.pixel_format.type = kOutputStreamMlFRPixelFormat;
  image_constraints.min_coded_width = kOutputStreamMlFRWidth;
  image_constraints.max_coded_width = kOutputStreamMlFRWidth;
  image_constraints.min_coded_height = kOutputStreamMlFRHeight;
  image_constraints.max_coded_height = kOutputStreamMlFRHeight;
  image_constraints.min_bytes_per_row = kOutputStreamMlFRWidth;
  image_constraints.max_bytes_per_row = kOutputStreamMlFRMaxBytesPerRow;
  image_constraints.layers = kOutputStreamMlFRLayers;
  image_constraints.bytes_per_row_divisor = kOutputStreamMlFRBytesPerRowDivisor;
  image_constraints.color_spaces_count = kOutputStreamMlFRColorSpaceCount;
  image_constraints.color_space[0].type = kOutputStreamMlFRColorSpaceType;
  constraints.usage.cpu = fuchsia::sysmem::cpuUsageWrite | fuchsia::sysmem::cpuUsageRead;
  return constraints;
}

static std::vector<fuchsia::sysmem::ImageFormat_2> OutputStreamMLFRImageFormats() {
  fuchsia::sysmem::ImageFormat_2 format{
      .pixel_format = {fuchsia::sysmem::PixelFormatType::NV12},
      .coded_width = kOutputStreamMlFRWidth,
      .coded_height = kOutputStreamMlFRHeight,
      .bytes_per_row = kOutputStreamMlFRWidth,
  };
  std::vector<fuchsia::sysmem::ImageFormat_2> ret_vec;
  ret_vec.push_back(format);
  return ret_vec;
}

static fuchsia::camera2::hal::StreamConfig OutputStreamMLFRConfig() {
  fuchsia::camera2::hal::StreamConfig stream_config;
  stream_config.frame_rate = {
      .frames_per_sec_numerator = kOutputStreamMlFRFrameRate,
      .frames_per_sec_denominator = 1,
  };
  stream_config.constraints = OutputStreamMLFRConstraints();
  stream_config.properties =
      GetStreamProperties(fuchsia::camera2::CameraStreamType::FULL_RESOLUTION |
                          fuchsia::camera2::CameraStreamType::MACHINE_LEARNING);
  stream_config.image_formats = OutputStreamMLFRImageFormats();
  return stream_config;
};

/***********************************
 * Output Stream ML DS paramters   *
 ***********************************
 */

static constexpr fuchsia::sysmem::BufferCollectionConstraints OutputStreamMLDSConstraints() {
  fuchsia::sysmem::BufferCollectionConstraints constraints;
  constraints.min_buffer_count_for_camping = kOutputStreamMlDSMinBufferForCamping;
  constraints.image_format_constraints_count = 1;
  auto& image_constraints = constraints.image_format_constraints[0];
  image_constraints.pixel_format.type = kOutputStreamMlDSPixelFormat;
  image_constraints.min_coded_width = kOutputStreamMlDSWidth;
  image_constraints.max_coded_width = kOutputStreamMlDSWidth;
  image_constraints.min_coded_height = kOutputStreamMlDSHeight;
  image_constraints.max_coded_height = kOutputStreamMlDSHeight;
  image_constraints.min_bytes_per_row = kOutputStreamMlDSWidth;
  image_constraints.max_bytes_per_row = kOutputStreamMlDSMaxBytesPerRow;
  image_constraints.layers = kOutputStreamMlDSLayers;
  image_constraints.bytes_per_row_divisor = kOutputStreamMlDSBytesPerRowDivisor;
  image_constraints.color_spaces_count = kOutputStreamMlDSColorSpaceCount;
  image_constraints.color_space[0].type = kOutputStreamMlDSColorSpaceType;
  constraints.usage.cpu = fuchsia::sysmem::cpuUsageWrite | fuchsia::sysmem::cpuUsageRead;
  return constraints;
}

static std::vector<fuchsia::sysmem::ImageFormat_2> OutputStreamMLDSImageFormats() {
  fuchsia::sysmem::ImageFormat_2 format{
      .pixel_format = {fuchsia::sysmem::PixelFormatType::NV12},
      .coded_width = kOutputStreamMlDSWidth,
      .coded_height = kOutputStreamMlDSHeight,
      .bytes_per_row = kOutputStreamMlDSWidth,
  };
  std::vector<fuchsia::sysmem::ImageFormat_2> ret_vec;
  ret_vec.push_back(format);
  return ret_vec;
}

static fuchsia::camera2::hal::StreamConfig OutputStreamMLDSConfig() {
  fuchsia::camera2::hal::StreamConfig stream_config;
  stream_config.frame_rate = {
      .frames_per_sec_numerator = kOutputStreamMonitoringFrameRate,
      .frames_per_sec_denominator = 1,
  };
  stream_config.constraints = OutputStreamMLDSConstraints();
  stream_config.properties =
      GetStreamProperties(fuchsia::camera2::CameraStreamType::DOWNSCALED_RESOLUTION |
                          fuchsia::camera2::CameraStreamType::MACHINE_LEARNING);
  stream_config.image_formats = OutputStreamMLDSImageFormats();
  return stream_config;
};

/******************************************
 * Output Stream DS Monitoring paramters  *
 ******************************************
 */

static constexpr fuchsia::sysmem::BufferCollectionConstraints
OutputStreamDSMonitoringConstraints() {
  fuchsia::sysmem::BufferCollectionConstraints constraints;
  constraints.min_buffer_count_for_camping = kOutputStreamMonitoringMinBufferForCamping;
  constraints.image_format_constraints_count = 1;
  auto& image_constraints = constraints.image_format_constraints[0];
  image_constraints.pixel_format.type = kOutputStreamMonitoringPixelFormat;
  image_constraints.min_coded_width = kOutputStreamMonitoringWidth;
  image_constraints.max_coded_width = kOutputStreamMonitoringWidth;
  image_constraints.min_coded_height = kOutputStreamMonitoringHeight;
  image_constraints.max_coded_height = kOutputStreamMonitoringHeight;
  image_constraints.min_bytes_per_row = kOutputStreamMonitoringWidth;
  image_constraints.max_bytes_per_row = kOutputStreamMonitoringMaxBytesPerRow;
  image_constraints.layers = kOutputStreamMonitoringLayers;
  image_constraints.bytes_per_row_divisor = kOutputStreamMonitoringBytesPerRowDivisor;
  image_constraints.color_spaces_count = kOutputStreamMonitoringColorSpaceCount;
  image_constraints.color_space[0].type = kOutputStreamMonitoringColorSpaceType;
  constraints.usage.cpu = fuchsia::sysmem::cpuUsageWrite | fuchsia::sysmem::cpuUsageRead;
  return constraints;
}

static std::vector<fuchsia::sysmem::ImageFormat_2> OutputStreamMonitoringImageFormats() {
  std::vector<fuchsia::sysmem::ImageFormat_2> ret_vec;
  fuchsia::sysmem::ImageFormat_2 format0{
      .pixel_format = {fuchsia::sysmem::PixelFormatType::NV12},
      .coded_width = kOutputStreamMonitoringWidth,
      .coded_height = kOutputStreamMonitoringHeight,
      .bytes_per_row = kOutputStreamMonitoringWidth,
  };
  fuchsia::sysmem::ImageFormat_2 format1{
      .pixel_format = {fuchsia::sysmem::PixelFormatType::NV12},
      .coded_width = kOutputStreamMonitoringWidth1,
      .coded_height = kOutputStreamMonitoringHeight1,
      .bytes_per_row = kOutputStreamMonitoringWidth1,
  };
  fuchsia::sysmem::ImageFormat_2 format2{
      .pixel_format = {fuchsia::sysmem::PixelFormatType::NV12},
      .coded_width = kOutputStreamMonitoringWidth2,
      .coded_height = kOutputStreamMonitoringHeight2,
      .bytes_per_row = kOutputStreamMonitoringWidth2,
  };

  ret_vec.push_back(format0);
  ret_vec.push_back(format1);
  ret_vec.push_back(format2);
  return ret_vec;
}

static fuchsia::camera2::hal::StreamConfig OutputStreamMonitoringConfig() {
  fuchsia::camera2::hal::StreamConfig stream_config;
  stream_config.frame_rate = {
      .frames_per_sec_numerator = kOutputStreamMonitoringFrameRate,
      .frames_per_sec_denominator = 1,
  };
  stream_config.constraints = OutputStreamDSMonitoringConstraints();
  stream_config.properties = GetStreamProperties(fuchsia::camera2::CameraStreamType::MONITORING);
  stream_config.image_formats = OutputStreamMonitoringImageFormats();
  return stream_config;
};

/*****************************
 *  EXTERNAL CONFIGURATIONS  *
 *****************************
 */

fuchsia::camera2::hal::Config MonitoringConfig() {
  fuchsia::camera2::hal::Config config;
  config.stream_configs.push_back(std::move(OutputStreamMLFRConfig()));
  config.stream_configs.push_back(std::move(OutputStreamMLDSConfig()));
  config.stream_configs.push_back(std::move(OutputStreamMonitoringConfig()));
  return config;
}

// ================== INTERNAL CONFIGURATION ======================== //
// FR --> OutputStreamMLFR (Directly from ISP) (10fps)
// FR --> GDC1 --> OutputStreamMLDS (10fps)

static InternalConfigNode OutputStreamMLFR() {
  InternalConfigNode node;
  node.type = kOutputStream;
  node.output_frame_rate.frames_per_sec_numerator = kOutputStreamMlFRFrameRate;
  node.output_frame_rate.frames_per_sec_denominator = 1;
  node.output_stream_type = fuchsia::camera2::CameraStreamType::FULL_RESOLUTION |
                            fuchsia::camera2::CameraStreamType::MACHINE_LEARNING;
  return node;
}

static InternalConfigNode OutputStreamMLDS() {
  InternalConfigNode node;
  node.type = kOutputStream;
  node.output_frame_rate.frames_per_sec_numerator = kOutputStreamMlDSFrameRate;
  node.output_frame_rate.frames_per_sec_denominator = 1;
  node.output_stream_type = fuchsia::camera2::CameraStreamType::DOWNSCALED_RESOLUTION |
                            fuchsia::camera2::CameraStreamType::MACHINE_LEARNING;
  return node;
}

static InternalConfigNode Gdc1() {
  InternalConfigNode node;
  node.type = kGdc;
  node.output_frame_rate.frames_per_sec_numerator = kOutputStreamMlDSFrameRate;
  node.output_frame_rate.frames_per_sec_denominator = 1;
  node.output_stream_type = fuchsia::camera2::CameraStreamType::DOWNSCALED_RESOLUTION |
                            fuchsia::camera2::CameraStreamType::MACHINE_LEARNING;
  node.child_nodes.push_back(std::move(OutputStreamMLDS()));
  node.gdc_info.config_type = DUMMY_CONFIG_0;
  return node;
}

InternalConfigNode MonitorConfigFullRes() {
  InternalConfigNode node;
  node.type = kInputStream;
  node.output_frame_rate.frames_per_sec_numerator = kOutputStreamMonitoringFrameRate;
  node.output_frame_rate.frames_per_sec_denominator = 1;
  node.input_stream_type = fuchsia::camera2::CameraStreamType::FULL_RESOLUTION;
  node.supported_streams.push_back(fuchsia::camera2::CameraStreamType::FULL_RESOLUTION |
                                   fuchsia::camera2::CameraStreamType::MACHINE_LEARNING);
  node.supported_streams.push_back(fuchsia::camera2::CameraStreamType::DOWNSCALED_RESOLUTION |
                                   fuchsia::camera2::CameraStreamType::MACHINE_LEARNING);
  node.child_nodes.push_back(std::move(OutputStreamMLFR()));
  node.child_nodes.push_back(std::move(Gdc1()));
  return node;
}

// DS --> GDC2 --> GE2D --> OutputStreamMonitoring

static InternalConfigNode OutputStreamMonitoring() {
  InternalConfigNode node;
  node.type = kOutputStream;
  node.output_frame_rate.frames_per_sec_numerator = kOutputStreamMonitoringFrameRate;
  node.output_frame_rate.frames_per_sec_denominator = 1;
  node.output_stream_type = fuchsia::camera2::CameraStreamType::VIDEO_CONFERENCE;
  return node;
}

static InternalConfigNode Gdc2() {
  InternalConfigNode node;
  node.type = kGdc;
  node.output_frame_rate.frames_per_sec_numerator = kOutputStreamMonitoringFrameRate;
  node.output_frame_rate.frames_per_sec_denominator = 1;
  node.output_stream_type = fuchsia::camera2::CameraStreamType::VIDEO_CONFERENCE;
  node.child_nodes.push_back(std::move(OutputStreamMonitoring()));
  node.gdc_info.config_type = DUMMY_CONFIG_1;
  return node;
}

InternalConfigNode MonitorConfigFDownScaledRes() {
  InternalConfigNode node;
  node.type = kInputStream;
  node.output_frame_rate.frames_per_sec_numerator = kOutputStreamMonitoringFrameRate;
  node.output_frame_rate.frames_per_sec_denominator = 1;
  node.input_stream_type = fuchsia::camera2::CameraStreamType::DOWNSCALED_RESOLUTION;
  node.supported_streams.push_back(fuchsia::camera2::CameraStreamType::VIDEO_CONFERENCE);
  node.child_nodes.push_back(std::move(Gdc2()));
  return node;
}

}  // namespace camera

#endif  // SRC_CAMERA_DRIVERS_CONTROLLER_CONFIGS_SHERLOCK_MONITORING_CONFIG_H_
