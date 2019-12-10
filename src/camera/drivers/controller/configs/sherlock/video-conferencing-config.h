// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <fuchsia/camera2/hal/cpp/fidl.h>
#include <fuchsia/sysmem/cpp/fidl.h>

#include <vector>

#include "common-util.h"
#include "src/camera/drivers/controller/configs/sherlock/internal-config.h"
#include "src/camera/lib/stream_utils/stream_constraints.h"

// This file contains static information for the Video conferencing configuration
// There are two streams in one configuration
// FR --> (30fps) GDC1 --> (5fps) GDC2 --> ML(640x512)
//                 :
//                 :
//                 ----> (30fps) GE2D --> Video conferencing client
// Not adding GE2D at the moment.

namespace camera {

namespace {

constexpr ::fuchsia::sysmem::PixelFormatType kFramePixelFormat =
    fuchsia::sysmem::PixelFormatType::NV12;

// Isp FR parameters
constexpr uint32_t kIspFRWidth = 2176;
constexpr uint32_t kIspFRHeight = 2720;

// GDC parameters
constexpr uint32_t kGdcFRWidth = 2240;
constexpr uint32_t kGdcFRHeight = 1792;

// ML Video FR Parameters
constexpr uint32_t kMlFRMinBufferForCamping = 5;
constexpr uint32_t kMlFRWidth = 640;
constexpr uint32_t kMlFRHeight = 512;
constexpr uint32_t kMlFRFrameRate = 5;

// Video Conferencing FR Parameters
constexpr uint32_t kVideoMinBufferForCamping = 5;
constexpr uint32_t kVideoWidth = 1280;
constexpr uint32_t kVideoHeight = 720;
constexpr uint32_t kVideoWidth1 = 896;
constexpr uint32_t kVideoHeight1 = 504;
constexpr uint32_t kVideoWidth2 = 640;
constexpr uint32_t kVideoHeight2 = 360;
constexpr uint32_t kVideoFrameRate = 30;

}  // namespace

/**********************************
 *  ML Video FR paramters         *
 **********************************
 */

static std::vector<fuchsia::sysmem::ImageFormat_2> MLFRImageFormats() {
  return {
      StreamConstraints::MakeImageFormat(kMlFRWidth, kMlFRHeight, kFramePixelFormat),
  };
}

static fuchsia::camera2::hal::StreamConfig MLVideoFRConfig() {
  StreamConstraints stream(fuchsia::camera2::CameraStreamType::FULL_RESOLUTION |
                           fuchsia::camera2::CameraStreamType::MACHINE_LEARNING |
                           fuchsia::camera2::CameraStreamType::VIDEO_CONFERENCE);
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

__UNUSED static std::vector<fuchsia::sysmem::ImageFormat_2> VideoImageFormats() {
  return {
      StreamConstraints::MakeImageFormat(kVideoWidth, kVideoHeight, kFramePixelFormat),
      StreamConstraints::MakeImageFormat(kVideoWidth1, kVideoHeight1, kFramePixelFormat),
      StreamConstraints::MakeImageFormat(kVideoWidth2, kVideoHeight2, kFramePixelFormat),
  };
}

static fuchsia::camera2::hal::StreamConfig VideoConfig() {
  StreamConstraints stream(fuchsia::camera2::CameraStreamType::VIDEO_CONFERENCE);
  stream.AddImageFormat(kVideoWidth, kVideoHeight, kFramePixelFormat);
  stream.AddImageFormat(kVideoWidth1, kVideoHeight1, kFramePixelFormat);
  stream.AddImageFormat(kVideoWidth2, kVideoHeight2, kFramePixelFormat);
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

fuchsia::camera2::hal::Config VideoConferencingConfig() {
  fuchsia::camera2::hal::Config config;
  config.stream_configs.push_back(MLVideoFRConfig());
  config.stream_configs.push_back(VideoConfig());
  return config;
}

// ================== INTERNAL CONFIGURATION ======================== //

static InternalConfigNode OutputMLFR() {
  return {
      .type = kOutputStream,
      .output_frame_rate.frames_per_sec_numerator = kMlFRFrameRate,
      .output_frame_rate.frames_per_sec_denominator = 1,
      .supported_streams =
          {
              fuchsia::camera2::CameraStreamType::FULL_RESOLUTION |
                  fuchsia::camera2::CameraStreamType::MACHINE_LEARNING |
                  fuchsia::camera2::CameraStreamType::VIDEO_CONFERENCE,
          },
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

static InternalConfigNode GdcVideo2() {
  return {
      .type = kGdc,
      .output_frame_rate.frames_per_sec_numerator = kVideoFrameRate,
      .output_frame_rate.frames_per_sec_denominator = 1,
      .supported_streams =
          {
              fuchsia::camera2::CameraStreamType::FULL_RESOLUTION |
                  fuchsia::camera2::CameraStreamType::MACHINE_LEARNING |
                  fuchsia::camera2::CameraStreamType::VIDEO_CONFERENCE,
          },
      .child_nodes =
          {
              {
                  OutputMLFR(),
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
  };
}

fuchsia::sysmem::BufferCollectionConstraints GdcVideo1InputConstraints() {
  StreamConstraints stream_constraints;
  stream_constraints.set_bytes_per_row_divisor(kGdcBytesPerRowDivisor);
  stream_constraints.set_contiguous(true);
  stream_constraints.AddImageFormat(kIspFRWidth, kIspFRHeight, kFramePixelFormat);
  stream_constraints.set_buffer_count_for_camping(0);
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
      StreamConstraints::MakeImageFormat(kIspFRWidth, kIspFRHeight, kFramePixelFormat),
  };
}

static InternalConfigNode GdcVideo1() {
  return {
      .type = kGdc,
      .output_frame_rate.frames_per_sec_numerator = kVideoFrameRate,
      .output_frame_rate.frames_per_sec_denominator = 1,
      .supported_streams =
          {
              fuchsia::camera2::CameraStreamType::FULL_RESOLUTION |
                  fuchsia::camera2::CameraStreamType::MACHINE_LEARNING |
                  fuchsia::camera2::CameraStreamType::VIDEO_CONFERENCE,
              fuchsia::camera2::CameraStreamType::VIDEO_CONFERENCE,
          },
      .child_nodes =
          {
              {
                  GdcVideo2(),
              },
          },
      .gdc_info.config_type =
          {
              GdcConfig::VIDEO_CONFERENCE,
          },
      .input_constraints = GdcVideo1InputConstraints(),
      .output_constraints = GdcVideo1OutputConstraints(),
      .image_formats = GdcVideo1ImageFormats(),
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

InternalConfigNode VideoConfigFullRes() {
  return {
      .type = kInputStream,
      .output_frame_rate.frames_per_sec_numerator = kVideoFrameRate,
      .output_frame_rate.frames_per_sec_denominator = 1,
      .input_stream_type = fuchsia::camera2::CameraStreamType::FULL_RESOLUTION,
      .supported_streams =
          {
              fuchsia::camera2::CameraStreamType::FULL_RESOLUTION |
                  fuchsia::camera2::CameraStreamType::MACHINE_LEARNING |
                  fuchsia::camera2::CameraStreamType::VIDEO_CONFERENCE,
              fuchsia::camera2::CameraStreamType::VIDEO_CONFERENCE,
          },
      .child_nodes =
          {
              {
                  GdcVideo1(),
              },
          },
      .input_constraints = InvalidConstraints(),
      .output_constraints = VideoConfigFullResConstraints(),
      .image_formats = IspImageFormats(),
  };
}

}  // namespace camera
