// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_CAMERA_BIN_USB_DEVICE_UTIL_CONFIG_H_
#define SRC_CAMERA_BIN_USB_DEVICE_UTIL_CONFIG_H_

#include <fuchsia/camera3/cpp/fidl.h>
#include <lib/fpromise/result.h>
#include <lib/syslog/cpp/macros.h>
#include <zircon/status.h>
#include <zircon/types.h>

#include "src/lib/fsl/handles/object_info.h"

namespace camera {

// Converts a camera2.hal.Config to a camera3.Configuration
inline fpromise::result<fuchsia::camera3::Configuration2, zx_status_t> Convert(
    const fuchsia::camera2::hal::Config& config) {
  if (config.stream_configs.empty()) {
    FX_LOGS(ERROR) << "Config reported no streams.";
    return fpromise::error(ZX_ERR_INTERNAL);
  }
  std::vector<fuchsia::camera3::StreamProperties2> streams;
  for (const auto& stream_config : config.stream_configs) {
    if (stream_config.image_formats.empty()) {
      FX_LOGS(ERROR) << "Stream reported no image formats.";
      return fpromise::error(ZX_ERR_INTERNAL);
    }
    fuchsia::camera3::StreamProperties2 stream_properties;
    stream_properties.set_frame_rate(
        {.numerator = stream_config.frame_rate.frames_per_sec_numerator,
         .denominator = stream_config.frame_rate.frames_per_sec_denominator});
    stream_properties.set_image_format(stream_config.image_formats[0]);
    // TODO(fxbug.dev/50908): Detect ROI support during initialization.
    stream_properties.set_supports_crop_region(true);
    std::vector<fuchsia::math::Size> supported_resolutions;
    for (const auto& format : stream_config.image_formats) {
      supported_resolutions.push_back({.width = static_cast<int32_t>(format.coded_width),
                                       .height = static_cast<int32_t>(format.coded_height)});
    }
    stream_properties.set_supported_resolutions(std::move(supported_resolutions));
    streams.push_back(std::move(stream_properties));
  }
  fuchsia::camera3::Configuration2 ret;
  ret.set_streams(std::move(streams));
  return fpromise::ok(std::move(ret));
}

// Converts a valid camera3.StreamProperties2 to a camera3.StreamProperties, dropping the
// |supported_resolutions| field.
inline fuchsia::camera3::StreamProperties Convert(
    const fuchsia::camera3::StreamProperties2& properties) {
  return {.image_format = properties.image_format(),
          .frame_rate = properties.frame_rate(),
          .supports_crop_region = properties.supports_crop_region()};
}

}  // namespace camera

#endif  // SRC_CAMERA_BIN_USB_DEVICE_UTIL_CONFIG_H_
