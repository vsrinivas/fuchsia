// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_CAMERA_STREAM_UTILS_CAMERA_STREAM_CONSTRAINTS_H_
#define SRC_CAMERA_STREAM_UTILS_CAMERA_STREAM_CONSTRAINTS_H_

#include <fuchsia/camera2/cpp/fidl.h>
#include <fuchsia/camera2/hal/cpp/fidl.h>
#include <fuchsia/sysmem/cpp/fidl.h>

#include <vector>

namespace camera {

// CameraStreamConstraints provides an easier way to specify constraints,
// using the limited set of data that is relevant to camera streams.
// Usage: To fill out a vector of camera configs:
// std::vector<fuchsia::camera2::hal::Config> configs(<number of configs>);
//
// For each stream config, specify the stream type, and add image formats:
// CameraStreamConstraints stream(fuchsia::camera2::CameraStreamType::MONITORING);
// stream.AddImageFormat(640, 512, fuchsia::sysmem::PixelFormatType::NV12);
// stream.AddImageFormat(896, 1600, fuchsia::sysmem::PixelFormatType::NV12);
// configs[0].stream_configs.push_back(stream.ConvertToStreamConfig());
//
// NOTE: The default settings for stream configs is below
//    |bytes_per_row_divisor_| = 128;
//    |buffer_count_for_camping_| = 3;
//    |frames_per_second_| = 30;
// If you need to use different settings, please use the setter functions
// to update, before you call |CameraStreamConstraints|

struct CameraStreamConstraints {
 public:
  CameraStreamConstraints(fuchsia::camera2::CameraStreamType type) : stream_type_(type) {}

  void AddImageFormat(uint32_t width, uint32_t height, fuchsia::sysmem::PixelFormatType format);

  void set_contiguous(bool flag) { contiguous_ = flag; }
  void set_bytes_per_row_divisor(uint32_t bytes_per_row_divisor) {
    bytes_per_row_divisor_ = bytes_per_row_divisor;
  }

  void set_frames_per_second(uint32_t frames_per_second) { frames_per_second_ = frames_per_second; }

  // Converts the data in this struct into a StreamConfig.
  fuchsia::camera2::hal::StreamConfig ConvertToStreamConfig();

 private:
  uint32_t bytes_per_row_divisor_ = 128;
  uint32_t buffer_count_for_camping_ = 3;
  uint32_t frames_per_second_ = 30;
  bool contiguous_ = false;
  bool cpu_access_ = true;
  std::vector<fuchsia::sysmem::ImageFormat_2> formats_;
  fuchsia::camera2::CameraStreamType stream_type_ = {};
};

}  // namespace camera

#endif  // SRC_CAMERA_STREAM_UTILS_CAMERA_STREAM_CONSTRAINTS_H_
