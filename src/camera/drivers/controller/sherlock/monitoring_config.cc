// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/camera/drivers/controller/sherlock/monitoring_config.h"

// This file contains static information for the Monitor Configuration
// There are three streams in one configuration
// FR --> OutputStreamMLFR (Directly from ISP) (10fps)
// FR --> GDC1 --> OutputStreamMLDS
// DS --> GDC2 --> GE2D --> OutputStreamMonitoring

namespace camera {

static InternalConfigNode Gdc1();
static InternalConfigNode Ge2dMonitoring();
static InternalConfigNode MLFRFrameSkipper();

/**********************************
 * Output Stream ML FR parameters *
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
  stream.set_bytes_per_row_divisor(kIspBytesPerRowDivisor);
  stream.set_contiguous(true);
  stream.set_frames_per_second(kOutputStreamMlFRFrameRate);
  // TODO(fxbug.dev/99578): Support releasing initial participants' camping buffer counts.
  stream.set_buffer_count_for_camping(kMonitoringIspFROutputBuffers + kMonitoringGDC1InputBuffers);
  stream.set_min_buffer_count(kMonitoringIspFROutputBuffers + kMonitoringGDC1InputBuffers +
                              kOutputStreamMlFRMaxClientBuffers);
  return stream.ConvertToStreamConfig();
}

/***********************************
 * Output Stream ML DS parameters  *
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
  stream.set_buffer_count_for_camping(kMonitoringGDC1OutputBuffers);
  stream.set_min_buffer_count(kMonitoringGDC1OutputBuffers + kOutputStreamMlDSMaxClientBuffers);
  return stream.ConvertToStreamConfig();
}

/******************************************
 * Output Stream DS Monitoring parameters *
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
  stream.set_bytes_per_row_divisor(kGe2dBytesPerRowDivisor);
  stream.set_contiguous(true);
  stream.set_frames_per_second(kMonitoringThrottledOutputFrameRate);
  stream.set_buffer_count_for_camping(kMonitoringGDC2OutputBuffers + kMonitoringGE2DInputBuffers);
  stream.set_min_buffer_count(kMonitoringGDC2OutputBuffers + kMonitoringGE2DInputBuffers +
                              kOutputStreamMonitoringMaxClientBuffers);
  return stream.ConvertToStreamConfig();
}

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
      .output_frame_rate =
          {
              .frames_per_sec_numerator = kOutputStreamMlFRFrameRate,
              .frames_per_sec_denominator = 1,
          },
      .supported_streams =
          {
              {
                  .type = fuchsia::camera2::CameraStreamType::FULL_RESOLUTION |
                          fuchsia::camera2::CameraStreamType::MACHINE_LEARNING,
                  .supports_dynamic_resolution = false,
                  .supports_crop_region = false,
              },
          },
      .input_constraints = CopyConstraintsWithOverrides(
          MonitoringConfig().stream_configs[0].constraints, {.min_buffer_count_for_camping = 0}),
      .image_formats = OutputStreamMLFRImageFormats(),
  };
}

static InternalConfigNode OutputStreamMLDS() {
  return {
      .type = kOutputStream,
      .output_frame_rate =
          {
              .frames_per_sec_numerator = kOutputStreamMlDSFrameRate,
              .frames_per_sec_denominator = 1,
          },
      .supported_streams =
          {
              {
                  .type = fuchsia::camera2::CameraStreamType::DOWNSCALED_RESOLUTION |
                          fuchsia::camera2::CameraStreamType::MACHINE_LEARNING,
                  .supports_dynamic_resolution = false,
                  .supports_crop_region = false,
              },
          },
      .input_constraints = CopyConstraintsWithOverrides(
          MonitoringConfig().stream_configs[1].constraints, {.min_buffer_count_for_camping = 0}),
      .image_formats = OutputStreamMLDSImageFormats(),
  };
}

fuchsia::sysmem::BufferCollectionConstraints Gdc1Constraints() {
  StreamConstraints stream_constraints;
  stream_constraints.set_bytes_per_row_divisor(kGdcBytesPerRowDivisor);
  stream_constraints.set_contiguous(true);
  stream_constraints.AddImageFormat(kOutputStreamMlFRWidth, kOutputStreamMlFRHeight,
                                    kOutputStreamMlFRPixelFormat);
  stream_constraints.set_buffer_count_for_camping(kMonitoringGDC1InputBuffers);
  return stream_constraints.MakeBufferCollectionConstraints();
}

static InternalConfigNode Gdc1() {
  return {
      .type = kGdc,
      .output_frame_rate =
          {
              .frames_per_sec_numerator = kOutputStreamMlDSFrameRate,
              .frames_per_sec_denominator = 1,
          },
      .supported_streams =
          {
              {
                  .type = fuchsia::camera2::CameraStreamType::DOWNSCALED_RESOLUTION |
                          fuchsia::camera2::CameraStreamType::MACHINE_LEARNING,
                  .supports_dynamic_resolution = false,
                  .supports_crop_region = false,
              },
          },
      .child_nodes =
          {
              {
                  OutputStreamMLDS(),
              },
          },
      .gdc_info =
          {
              .config_type =
                  {
                      GdcConfig::MONITORING_ML,
                  },
          },
      .input_constraints = Gdc1Constraints(),
      .output_constraints = CopyConstraintsWithOverrides(
          OutputStreamMLDSConfig().constraints,
          {.min_buffer_count_for_camping = kMonitoringGDC1OutputBuffers}),
      .image_formats = OutputStreamMLDSImageFormats(),
  };
}

fuchsia::sysmem::BufferCollectionConstraints MonitorConfigFullResConstraints() {
  StreamConstraints stream_constraints;
  stream_constraints.set_bytes_per_row_divisor(kIspBytesPerRowDivisor);
  stream_constraints.set_contiguous(true);
  stream_constraints.AddImageFormat(kOutputStreamMlFRWidth, kOutputStreamMlFRHeight,
                                    kOutputStreamMlFRPixelFormat);
  stream_constraints.set_buffer_count_for_camping(kMonitoringIspFROutputBuffers);
  return stream_constraints.MakeBufferCollectionConstraints();
}

InternalConfigNode MonitorConfigFullRes() {
  return {
      .type = kInputStream,
      .output_frame_rate =
          {
              .frames_per_sec_numerator = kSensorMaxFramesPerSecond,
              .frames_per_sec_denominator = 1,
          },
      .input_stream_type = fuchsia::camera2::CameraStreamType::FULL_RESOLUTION,
      .supported_streams =
          {
              {
                  .type = fuchsia::camera2::CameraStreamType::FULL_RESOLUTION |
                          fuchsia::camera2::CameraStreamType::MACHINE_LEARNING,
                  .supports_dynamic_resolution = false,
                  .supports_crop_region = false,
              },
              {
                  .type = fuchsia::camera2::CameraStreamType::DOWNSCALED_RESOLUTION |
                          fuchsia::camera2::CameraStreamType::MACHINE_LEARNING,
                  .supports_dynamic_resolution = false,
                  .supports_crop_region = false,
              },
          },
      .child_nodes =
          {
              {
                  MLFRFrameSkipper(),
              },
          },
      // input constrains not applicable
      .output_constraints = MonitorConfigFullResConstraints(),
      .image_formats = OutputStreamMLFRImageFormats(),
  };
}

static InternalConfigNode MLFRFrameSkipper() {
  // This node serves to synchronize the skipped frames between the two reduced-fps downstream
  // nodes.
  static_assert(kOutputStreamMlFRFrameRate == kOutputStreamMlDSFrameRate);
  return {
      .type = kPassthrough,
      .output_frame_rate =
          {
              .frames_per_sec_numerator = kOutputStreamMlFRFrameRate,
              .frames_per_sec_denominator = 1,
          },
      .input_stream_type = fuchsia::camera2::CameraStreamType::FULL_RESOLUTION,
      .supported_streams =
          {
              {
                  .type = fuchsia::camera2::CameraStreamType::FULL_RESOLUTION |
                          fuchsia::camera2::CameraStreamType::MACHINE_LEARNING,
                  .supports_dynamic_resolution = false,
                  .supports_crop_region = false,
              },
              {
                  .type = fuchsia::camera2::CameraStreamType::DOWNSCALED_RESOLUTION |
                          fuchsia::camera2::CameraStreamType::MACHINE_LEARNING,
                  .supports_dynamic_resolution = false,
                  .supports_crop_region = false,
              },
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
  };
}

// DS --> GDC2 --> GE2D --> OutputStreamMonitoring

static InternalConfigNode OutputStreamMonitoring() {
  return {
      .type = kOutputStream,
      .output_frame_rate =
          {
              .frames_per_sec_numerator = kMonitoringThrottledOutputFrameRate,
              .frames_per_sec_denominator = 1,
          },
      .supported_streams =
          {
              {
                  .type = fuchsia::camera2::CameraStreamType::MONITORING,
                  .supports_dynamic_resolution = false,
                  .supports_crop_region = false,
              },
          },
      .input_constraints = CopyConstraintsWithOverrides(
          MonitoringConfig().stream_configs[2].constraints, {.min_buffer_count_for_camping = 0}),
      .image_formats = OutputStreamMonitoringImageFormats(),
  };
}

fuchsia::sysmem::BufferCollectionConstraints Gdc2Constraints() {
  StreamConstraints stream_constraints;
  stream_constraints.set_bytes_per_row_divisor(kGdcBytesPerRowDivisor);
  stream_constraints.set_contiguous(true);
  stream_constraints.AddImageFormat(kOutputStreamDSWidth, kOutputStreamDSHeight,
                                    kOutputStreamMonitoringPixelFormat);
  stream_constraints.set_buffer_count_for_camping(kMonitoringGDC2InputBuffers);
  return stream_constraints.MakeBufferCollectionConstraints();
}

fuchsia::sysmem::BufferCollectionConstraints Ge2dMonitoringConstraints() {
  StreamConstraints stream_constraints;
  stream_constraints.set_bytes_per_row_divisor(kGe2dBytesPerRowDivisor);
  stream_constraints.set_contiguous(true);
  stream_constraints.AddImageFormat(kOutputStreamMonitoringWidth, kOutputStreamMonitoringHeight,
                                    kOutputStreamMonitoringPixelFormat);
  stream_constraints.AddImageFormat(kOutputStreamMonitoringWidth1, kOutputStreamMonitoringHeight1,
                                    kOutputStreamMonitoringPixelFormat);
  stream_constraints.AddImageFormat(kOutputStreamMonitoringWidth2, kOutputStreamMonitoringHeight2,
                                    kOutputStreamMonitoringPixelFormat);
  stream_constraints.set_buffer_count_for_camping(kMonitoringGE2DInputBuffers);
  return stream_constraints.MakeBufferCollectionConstraints();
}

static InternalConfigNode Ge2dMonitoring() {
  return {
      .type = kGe2d,
      .output_frame_rate =
          {
              .frames_per_sec_numerator = kMonitoringThrottledOutputFrameRate,
              .frames_per_sec_denominator = 1,
          },
      .supported_streams =
          {
              {
                  .type = fuchsia::camera2::CameraStreamType::MONITORING,
                  .supports_dynamic_resolution = false,
                  .supports_crop_region = false,
              },
          },
      .child_nodes =
          {
              {
                  OutputStreamMonitoring(),
              },
          },
      .ge2d_info =
          {
              .config_type = Ge2DConfig::GE2D_WATERMARK,
              .watermark =
                  {
                      {
                          .filename = "watermark-720p.rgba",
                          .image_format = StreamConstraints::MakeImageFormat(
                              kWatermark720pWidth, kWatermark720pHeight, kWatermarkPixelFormat),
                          .loc_x = kOutputStreamMonitoringWidth - kWatermark720pWidth,
                          .loc_y = 0,
                      },
                      {
                          .filename = "watermark-480p.rgba",
                          .image_format = StreamConstraints::MakeImageFormat(
                              kWatermark480pWidth, kWatermark480pHeight, kWatermarkPixelFormat),
                          .loc_x = kOutputStreamMonitoringWidth1 - kWatermark480pWidth,
                          .loc_y = 0,
                      },
                      {
                          .filename = "watermark-360p.rgba",
                          .image_format = StreamConstraints::MakeImageFormat(
                              kWatermark360pWidth, kWatermark360pHeight, kWatermarkPixelFormat),
                          .loc_x = kOutputStreamMonitoringWidth2 - kWatermark360pWidth,
                          .loc_y = 0,
                      },
                  },
          },
      // Ge2dMonitoringConstraints uses kMonitoringGE2DInputBuffers, so modification is necessary to
      // reflect the output constraints.
      .input_constraints = Ge2dMonitoringConstraints(),
      .image_formats = OutputStreamMonitoringImageFormats(),
  };
}

fuchsia::sysmem::BufferCollectionConstraints Gdc2OutputConstraints() {
  StreamConstraints stream_constraints;
  stream_constraints.set_bytes_per_row_divisor(kGdcBytesPerRowDivisor);
  stream_constraints.set_contiguous(true);
  stream_constraints.AddImageFormat(kOutputStreamMonitoringWidth, kOutputStreamMonitoringHeight,
                                    kOutputStreamMonitoringPixelFormat);
  stream_constraints.AddImageFormat(kOutputStreamMonitoringWidth1, kOutputStreamMonitoringHeight1,
                                    kOutputStreamMonitoringPixelFormat);
  stream_constraints.AddImageFormat(kOutputStreamMonitoringWidth2, kOutputStreamMonitoringHeight2,
                                    kOutputStreamMonitoringPixelFormat);
  stream_constraints.set_buffer_count_for_camping(kMonitoringGDC2OutputBuffers);
  return stream_constraints.MakeBufferCollectionConstraints();
}

static InternalConfigNode Gdc2() {
  return {
      .type = kGdc,
      .output_frame_rate =
          {
              .frames_per_sec_numerator = kMonitoringThrottledOutputFrameRate,
              .frames_per_sec_denominator = 1,
          },
      .supported_streams =
          {
              {
                  .type = fuchsia::camera2::CameraStreamType::MONITORING,
                  .supports_dynamic_resolution = true,
                  .supports_crop_region = false,
              },
          },
      .child_nodes =
          {
              {
                  Ge2dMonitoring(),
              },
          },
      .gdc_info =
          {
              .config_type =
                  {
                      GdcConfig::MONITORING_720p,
                      GdcConfig::MONITORING_480p,
                      GdcConfig::MONITORING_360p,
                  },
          },
      .input_constraints = Gdc2Constraints(),
      .output_constraints = Gdc2OutputConstraints(),
      .image_formats = OutputStreamMonitoringImageFormats(),
  };
}

fuchsia::sysmem::BufferCollectionConstraints MonitorConfigDownScaledResConstraints() {
  StreamConstraints stream_constraints;
  stream_constraints.set_bytes_per_row_divisor(kIspBytesPerRowDivisor);
  stream_constraints.set_contiguous(true);
  stream_constraints.AddImageFormat(kOutputStreamDSWidth, kOutputStreamDSHeight,
                                    kOutputStreamMonitoringPixelFormat);
  stream_constraints.set_buffer_count_for_camping(kMonitoringIspDSOutputBuffers);
  return stream_constraints.MakeBufferCollectionConstraints();
}

InternalConfigNode MonitorConfigDownScaledRes() {
  return {
      .type = kInputStream,
      .output_frame_rate =
          {
              .frames_per_sec_numerator = kSensorMaxFramesPerSecond,
              .frames_per_sec_denominator = 1,
          },
      .input_stream_type = fuchsia::camera2::CameraStreamType::DOWNSCALED_RESOLUTION,
      .supported_streams =
          {
              {
                  .type = fuchsia::camera2::CameraStreamType::MONITORING,
                  .supports_dynamic_resolution = false,
                  .supports_crop_region = false,
              },
          },
      .child_nodes =
          {
              {
                  Gdc2(),
              },
          },
      // input constraints not applicable
      .output_constraints = MonitorConfigDownScaledResConstraints(),
      .image_formats = MonitorConfigDownScaledResImageFormats(),
  };
}

}  // namespace camera
