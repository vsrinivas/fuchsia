// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_CAMERA_DRIVERS_CONTROLLER_STREAM_PIPELINE_INFO_H_
#define SRC_CAMERA_DRIVERS_CONTROLLER_STREAM_PIPELINE_INFO_H_

#include <fuchsia/camera2/cpp/fidl.h>
#include <fuchsia/camera2/hal/cpp/fidl.h>

#include "src/camera/drivers/controller/configs/internal_config.h"

namespace camera {

// |StreamCreationData| is populated runtime based on
// |config_index|, |stream_index| and |image_format_index| parameters
// when clients call into HAL to request for
// new streams. This structure is valid only during
// stream creation call.
struct StreamCreationData {
  // |node| is the head of the internal graph representing
  // the requested stream |stream_index| in config |config_index|.
  InternalConfigNode node;
  // |stream_config| has the stream properties of the requested stream.
  fuchsia::camera2::hal::StreamConfig stream_config;
  // |image_format_index| is the output resolution index
  // passed by clients when requesting a stream.
  uint32_t image_format_index;
  // Ouput buffers received from the client.
  fuchsia::sysmem::BufferCollectionInfo_2 output_buffers;
  // Allowed frame rate range for a particular configuration.
  camera::FrameRateRange frame_rate_range;

  // Returns the stream type of the requested stream.
  fuchsia::camera2::CameraStreamType stream_type() const {
    return stream_config.properties.stream_type();
  }
};

}  // namespace camera

#endif  // SRC_CAMERA_DRIVERS_CONTROLLER_STREAM_PIPELINE_INFO_H_
