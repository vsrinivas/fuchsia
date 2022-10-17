// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_CAMERA_DRIVERS_USB_VIDEO_DESCRIPTORS_H_
#define SRC_CAMERA_DRIVERS_USB_VIDEO_DESCRIPTORS_H_

#include <fuchsia/camera/cpp/fidl.h>
#include <fuchsia/hardware/usb/c/banjo.h>
#include <lib/zx/status.h>

#include <vector>

#include <usb/usb.h>
#include <usb/video.h>

namespace camera::usb_video {

enum class UvcPixelFormat {
  INVALID,  // default value, not supported
  BGRA32,   // 32bpp BGRA, 1 plane.
  I420,     // 8 bit Y plane followed by 8 bit 2×2 subsampled U and V
  M420,     // pseudo planar: 4:2:0 sampling, with 2 Y lines then 1 CbCr line
  NV12,     // 8-bit Y plane followed by an interleaved U/V plane with 2×2 subsampling
  YUY2,     // nonplanar 4:2:2 with ordering YUYV
  MJPEG     // MJPEG encoded image
};

// Decoded video dimensions and other frame-specific characteristics
// supported by frame-based formats.
struct UvcFormat {
  // Frame description, for usb reference:
  const uint8_t format_index;
  const uint8_t frame_index;

  const UvcPixelFormat pixel_format;
  const uint8_t bits_per_pixel;

  // Specified in 100ns units.
  const uint32_t default_frame_interval;
  const uint16_t width;
  const uint16_t height;
  // The number of bytes per line of video.
  const uint32_t stride;
  const uint8_t default_frame_index;

  fuchsia::camera::VideoFormat ToFidl() const;
  bool operator==(const fuchsia::camera::VideoFormat& vf) const;
};

// For storing characteristics of a video streaming interface and its
// underlying isochronous endpoint.
struct StreamingEndpointSetting {
  const uint8_t address;
  const uint8_t alt_setting;
  // Only meaningful for isochronous endpoints:
  const uint32_t isoc_bandwidth;

  // USB_ENDPOINT_BULK or USB_ENDPOINT_ISOCHRONOUS
  const int ep_type;
};

struct StreamingSetting {
  uint32_t hw_clock_frequency = 0;
  usb_interface_info_descriptor_t vs_interface;
  usb_video_vs_input_header_desc_short input_header;
  std::vector<UvcFormat> formats;
  std::vector<StreamingEndpointSetting> endpoint_settings;
  StreamingSetting(const StreamingSetting& s) = delete;
  StreamingSetting(StreamingSetting&& s) = default;
  StreamingSetting() = default;
};

zx::result<StreamingSetting> LoadStreamingSettings(usb_desc_iter_t* iter);

}  // namespace camera::usb_video

#endif  // SRC_CAMERA_DRIVERS_USB_VIDEO_DESCRIPTORS_H_
