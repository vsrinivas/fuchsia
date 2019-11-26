// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_CAMERA_DRIVERS_CONTROLLER_CONFIGS_SHERLOCK_INTERNAL_CONFIG_H_
#define SRC_CAMERA_DRIVERS_CONTROLLER_CONFIGS_SHERLOCK_INTERNAL_CONFIG_H_

#include <fuchsia/camera2/hal/cpp/fidl.h>
#include <fuchsia/sysmem/cpp/fidl.h>

#include <vector>

#include <ddktl/protocol/gdc.h>
#include <ddktl/protocol/ge2d.h>

namespace camera {

enum GdcConfig {
  MONITORING_360p = 0,                // config_1152x1440_to_512x384_Crop_Rotate
  MONITORING_480p = 1,                // config_1152x1440_to_720x540_Crop_Rotate
  MONITORING_720p = 2,                // config_1152x1440_to_1152x864_Crop_Rotate
  MONITORING_ML = 3,                  // config_001_2176x2720-to-640x512-RS-YUV420SemiPlanar
  VIDEO_CONFERENCE = 4,               // config_002_2176x2720-to-2240x1792-DKCR-YUV420SemiPlanar
  VIDEO_CONFERENCE_EXTENDED_FOV = 5,  // config_003_2176x2720-to-2240x1792-DKCR-YUV420SemiPlanar
  VIDEO_CONFERENCE_ML = 6,            // config_001_2240x1792-to-640x512-S-YUV420SemiPlanar
  INVALID = 7,
};

enum Ge2DConfig {
  GE2D_WATERMARK = 0,
  GE2D_RESIZE = 1,
  GE2D_COUNT = 2,
};
struct GdcInfo {
  std::vector<GdcConfig> config_type;
};
struct Ge2DInfo {
  Ge2DConfig config_type;
  water_mark_info watermark;
  resize_info resize;
};

enum NodeType {
  kInputStream,
  kGdc,
  kGe2d,
  kOutputStream,
};
struct InternalConfigNode {
  // To identify the type of the node this is.
  NodeType type;
  // To identify the input frame rate at this node.
  fuchsia::camera2::FrameRate output_frame_rate;
  // This is only valid for Input Stream Type to differentiate
  // between ISP FR/DS/Scalar streams.
  fuchsia::camera2::CameraStreamType input_stream_type;
  // Types of |stream_types| supported by this node.
  std::vector<fuchsia::camera2::CameraStreamType> supported_streams;
  // Child nodes
  std::vector<InternalConfigNode> child_nodes;
  // HWAccelerator Info if applicable.
  Ge2DInfo ge2d_info;
  GdcInfo gdc_info;
  // Constraints
  fuchsia::sysmem::BufferCollectionConstraints constraints;
  // Image formats supported
  std::vector<fuchsia::sysmem::ImageFormat_2> image_formats;
};

struct InternalConfigInfo {
  // List of all the streams part of this configuration
  // These streams are high level streams which are coming in from
  // the ISP and not the output streams which are provided to the clients.
  // That information is part of this |InternalConfigNode| in |supported_streams|
  std::vector<InternalConfigNode> streams_info;
};

struct InternalConfigs {
  // List of all the configurations supported on a particular platform
  // Order of the configuration needs to match with the external configuration
  // data.
  std::vector<InternalConfigInfo> configs_info;
};

}  // namespace camera

#endif  // SRC_CAMERA_DRIVERS_CONTROLLER_CONFIGS_SHERLOCK_INTERNAL_CONFIG_H_
