// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/camera/drivers/controller/sherlock/video_conferencing_config.h"

// This file contains static information for the Video conferencing configuration
// There are two streams in one configuration
// FR --> (30fps) GDC1 --> (5fps) GDC2 --> ML(640x512)
//                 :
//                 :
//                 ----> (30fps) GE2D --> Video conferencing client
// Not adding GE2D at the moment.

namespace camera {

/**********************************
 *  ML Video FR paramters         *
 **********************************
 */

static std::vector<fuchsia::sysmem::ImageFormat_2> MLFRImageFormats() {
  return {
      StreamConstraints::MakeImageFormat(kMlFRWidth, kMlFRHeight, kFramePixelFormat),
  };
}

static fuchsia::camera2::hal::StreamConfig MLVideoFRConfig(bool extended_fov) {
  auto stream_properties = extended_fov
                               ? kMlStreamType | fuchsia::camera2::CameraStreamType::EXTENDED_FOV
                               : kMlStreamType;

  StreamConstraints stream(stream_properties);
  stream.AddImageFormat(kMlFRWidth, kMlFRHeight, kFramePixelFormat);
  stream.set_bytes_per_row_divisor(kGdcBytesPerRowDivisor);
  stream.set_contiguous(true);
  stream.set_frames_per_second(kMlFRFrameRate);
  stream.set_buffer_count_for_camping(kMlFRMinBufferForCamping);
  return stream.ConvertToStreamConfig();
}

/******************************************
 * Video Conferencing FR Parameters       *
 ******************************************
 */

static std::vector<fuchsia::sysmem::ImageFormat_2> VideoImageFormats() {
  return {
      StreamConstraints::MakeImageFormat(kVideoWidth, kVideoHeight, kFramePixelFormat),
      StreamConstraints::MakeImageFormat(kVideoWidth1, kVideoHeight1, kFramePixelFormat),
      StreamConstraints::MakeImageFormat(kVideoWidth2, kVideoHeight2, kFramePixelFormat),
  };
}

static fuchsia::camera2::hal::StreamConfig VideoConfig(bool extended_fov) {
  auto stream_properties = extended_fov
                               ? kVideoStreamType | fuchsia::camera2::CameraStreamType::EXTENDED_FOV
                               : kVideoStreamType;

  StreamConstraints stream(stream_properties);
  stream.AddImageFormat(kVideoWidth, kVideoHeight, kFramePixelFormat, kGdcFRWidth, kGdcFRHeight);
  stream.AddImageFormat(kVideoWidth1, kVideoHeight1, kFramePixelFormat, kGdcFRWidth, kGdcFRHeight);
  stream.AddImageFormat(kVideoWidth2, kVideoHeight2, kFramePixelFormat, kGdcFRWidth, kGdcFRHeight);
  stream.set_bytes_per_row_divisor(kGdcBytesPerRowDivisor);
  stream.set_contiguous(true);
  stream.set_frames_per_second(kVideoFrameRate);
  stream.set_buffer_count_for_camping(kVideoMinBufferForCamping);
  return stream.ConvertToStreamConfig();
};

/*****************************
 *  EXTERNAL CONFIGURATIONS  *
 *****************************
 */

fuchsia::camera2::hal::Config VideoConferencingConfig(bool extended_fov) {
  fuchsia::camera2::hal::Config config;
  config.stream_configs.push_back(MLVideoFRConfig(extended_fov));
  config.stream_configs.push_back(VideoConfig(extended_fov));
  return config;
}

// ================== INTERNAL CONFIGURATION ======================== //

static InternalConfigNode OutputMLFR(bool extended_fov) {
  return {
      .type = kOutputStream,
      .output_frame_rate.frames_per_sec_numerator = kMlFRFrameRate,
      .output_frame_rate.frames_per_sec_denominator = 1,
      .supported_streams =
          {
              {
                  .type = extended_fov
                              ? kMlStreamType | fuchsia::camera2::CameraStreamType::EXTENDED_FOV
                              : kMlStreamType,
                  .supports_dynamic_resolution = false,
                  .supports_crop_region = false,
              },
          },
      .image_formats = MLFRImageFormats(),
      .in_place = false,
  };
}

static InternalConfigNode OutputVideoConferencing(bool extended_fov) {
  return {
      .type = kOutputStream,
      .output_frame_rate.frames_per_sec_numerator = kVideoFrameRate,
      .output_frame_rate.frames_per_sec_denominator = 1,
      .supported_streams =
          {
              {
                  .type = extended_fov
                              ? kVideoStreamType | fuchsia::camera2::CameraStreamType::EXTENDED_FOV
                              : kVideoStreamType,
                  .supports_dynamic_resolution = false,
                  .supports_crop_region = false,
              },
          },
      .image_formats = VideoImageFormats(),
      .in_place = false,
  };
}

fuchsia::sysmem::BufferCollectionConstraints GdcVideo2Constraints() {
  StreamConstraints stream_constraints;
  stream_constraints.set_bytes_per_row_divisor(kGdcBytesPerRowDivisor);
  stream_constraints.set_contiguous(true);
  stream_constraints.AddImageFormat(kGdcFRWidth, kGdcFRHeight, kFramePixelFormat);
  stream_constraints.set_buffer_count_for_camping(kVideoMinBufferForCamping);
  return stream_constraints.MakeBufferCollectionConstraints();
}

static InternalConfigNode GdcVideo2(bool extended_fov) {
  return {
      .type = kGdc,
      .output_frame_rate.frames_per_sec_numerator = kMlFRFrameRate,
      .output_frame_rate.frames_per_sec_denominator = 1,
      .supported_streams =
          {
              {
                  .type = extended_fov
                              ? kMlStreamType | fuchsia::camera2::CameraStreamType::EXTENDED_FOV
                              : kMlStreamType,
                  .supports_dynamic_resolution = false,
                  .supports_crop_region = false,
              },
          },
      .child_nodes =
          {
              {
                  OutputMLFR(extended_fov),
              },
          },
      .gdc_info.config_type =
          {
              GdcConfig::VIDEO_CONFERENCE_ML,
          },
      .input_constraints = GdcVideo2Constraints(),
      // This node doesn't need |output_constraints| because next node is Output node so
      // there is no need to create internal buffers.
      .output_constraints = InvalidConstraints(),
      .image_formats = MLFRImageFormats(),
      .in_place = false,
  };
}

fuchsia::sysmem::BufferCollectionConstraints Ge2dConstraints() {
  StreamConstraints stream_constraints;
  stream_constraints.set_bytes_per_row_divisor(kGe2dBytesPerRowDivisor);
  stream_constraints.set_contiguous(true);
  stream_constraints.AddImageFormat(kGdcFRWidth, kGdcFRHeight, kFramePixelFormat);
  stream_constraints.set_buffer_count_for_camping(kVideoMinBufferForCamping);
  return stream_constraints.MakeBufferCollectionConstraints();
}

static InternalConfigNode Ge2d(bool extended_fov) {
  return {
      .type = kGe2d,
      .output_frame_rate.frames_per_sec_numerator = kVideoFrameRate,
      .output_frame_rate.frames_per_sec_denominator = 1,
      .supported_streams =
          {
              {
                  .type = extended_fov
                              ? kVideoStreamType | fuchsia::camera2::CameraStreamType::EXTENDED_FOV
                              : kVideoStreamType,
                  .supports_dynamic_resolution = true,
                  .supports_crop_region = true,
              },
          },
      .child_nodes =
          {
              {
                  OutputVideoConferencing(extended_fov),
              },
          },
      .ge2d_info.config_type = Ge2DConfig::GE2D_RESIZE,
      .ge2d_info.resize =
          {
              .crop =
                  {
                      .x = 0,
                      .y = 0,
                      .width = kGdcFRWidth,
                      .height = kGdcFRHeight,
                  },
              .output_rotation = GE2D_ROTATION_ROTATION_0,
          },
      .input_constraints = Ge2dConstraints(),
      // This node doesn't need |output_constraints| because next node is Output node so
      // there is no need to create internal buffers.
      .output_constraints = InvalidConstraints(),
      .image_formats = VideoImageFormats(),
      .in_place = false,
  };
}

fuchsia::sysmem::BufferCollectionConstraints GdcVideo1InputConstraints() {
  StreamConstraints stream_constraints;
  stream_constraints.set_bytes_per_row_divisor(kGdcBytesPerRowDivisor);
  stream_constraints.set_contiguous(true);
  stream_constraints.AddImageFormat(kIspFRWidth, kIspFRHeight, kFramePixelFormat);
  stream_constraints.set_buffer_count_for_camping(kGdcBufferForCamping);
  return stream_constraints.MakeBufferCollectionConstraints();
}

fuchsia::sysmem::BufferCollectionConstraints GdcVideo1OutputConstraints() {
  StreamConstraints stream_constraints;
  stream_constraints.set_bytes_per_row_divisor(kGdcBytesPerRowDivisor);
  stream_constraints.set_contiguous(true);
  stream_constraints.AddImageFormat(kGdcFRWidth, kGdcFRHeight, kFramePixelFormat);
  stream_constraints.set_buffer_count_for_camping(0);
  return stream_constraints.MakeBufferCollectionConstraints();
}

static std::vector<fuchsia::sysmem::ImageFormat_2> GdcVideo1ImageFormats() {
  return {
      StreamConstraints::MakeImageFormat(kGdcFRWidth, kGdcFRHeight, kFramePixelFormat),
  };
}

static InternalConfigNode GdcVideo1(bool extended_fov) {
  auto gdc_config = GdcConfig::VIDEO_CONFERENCE;
  if (extended_fov) {
    gdc_config = GdcConfig::VIDEO_CONFERENCE_EXTENDED_FOV;
  }

  return {
      .type = kGdc,
      .output_frame_rate.frames_per_sec_numerator = kVideoFrameRate,
      .output_frame_rate.frames_per_sec_denominator = 1,
      .supported_streams =
          {
              {
                  .type = extended_fov
                              ? kMlStreamType | fuchsia::camera2::CameraStreamType::EXTENDED_FOV
                              : kMlStreamType,
                  .supports_dynamic_resolution = false,
                  .supports_crop_region = false,

              },
              {
                  .type = extended_fov
                              ? kVideoStreamType | fuchsia::camera2::CameraStreamType::EXTENDED_FOV
                              : kVideoStreamType,
                  .supports_dynamic_resolution = false,
                  .supports_crop_region = false,
              },
          },
      .child_nodes =
          {
              {
                  GdcVideo2(extended_fov),
              },
              {
                  Ge2d(extended_fov),
              },
          },
      .gdc_info.config_type =
          {
              gdc_config,
          },
      .input_constraints = GdcVideo1InputConstraints(),
      .output_constraints = GdcVideo1OutputConstraints(),
      .image_formats = GdcVideo1ImageFormats(),
      .in_place = false,
  };
}

fuchsia::sysmem::BufferCollectionConstraints VideoConfigFullResConstraints() {
  StreamConstraints stream_constraints;
  stream_constraints.set_bytes_per_row_divisor(kIspBytesPerRowDivisor);
  stream_constraints.set_contiguous(true);
  stream_constraints.AddImageFormat(kIspFRWidth, kIspFRHeight, kFramePixelFormat);
  stream_constraints.set_buffer_count_for_camping(kMlFRMinBufferForCamping);
  return stream_constraints.MakeBufferCollectionConstraints();
}

static std::vector<fuchsia::sysmem::ImageFormat_2> IspImageFormats() {
  return {
      StreamConstraints::MakeImageFormat(kIspFRWidth, kIspFRHeight, kFramePixelFormat),
  };
}

InternalConfigNode VideoConfigFullRes(bool extended_fov) {
  return {
      .type = kInputStream,
      .output_frame_rate.frames_per_sec_numerator = kVideoFrameRate,
      .output_frame_rate.frames_per_sec_denominator = 1,
      .input_stream_type = fuchsia::camera2::CameraStreamType::FULL_RESOLUTION,
      .supported_streams =
          {
              {
                  .type = extended_fov
                              ? kMlStreamType | fuchsia::camera2::CameraStreamType::EXTENDED_FOV
                              : kMlStreamType,
                  .supports_dynamic_resolution = false,
                  .supports_crop_region = false,
              },
              {
                  .type = extended_fov
                              ? kVideoStreamType | fuchsia::camera2::CameraStreamType::EXTENDED_FOV
                              : kVideoStreamType,
                  .supports_dynamic_resolution = false,
                  .supports_crop_region = false,
              },
          }  // namespace camera
      ,
      .child_nodes =
          {
              {
                  GdcVideo1(extended_fov),
              },
          },
      .input_constraints = InvalidConstraints(),
      .output_constraints = VideoConfigFullResConstraints(),
      .image_formats = IspImageFormats(),
      .in_place = false,
  };
}

}  // namespace camera
