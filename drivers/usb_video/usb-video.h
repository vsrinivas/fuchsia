// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <ddk/device.h>
#include <driver/usb.h>
#include <fbl/vector.h>
#include <zircon/compiler.h>
#include <zircon/device/camera-proto.h>
#include <zircon/hw/usb-video.h>
#include <zircon/hw/usb.h>

namespace video {
namespace usb {

// Decoded video dimensions and other frame-specific characteristics
// supported by frame-based formats.
struct UsbVideoFrameDesc {
  uint8_t index;

  camera::camera_proto::CaptureType capture_type;
  // Specified in 100ns units.
  uint32_t default_frame_interval;
  uint16_t width;
  uint16_t height;
  // The number of bytes per line of video.
  uint32_t stride;
};

struct UsbVideoFormat {
  uint8_t index;
  camera::camera_proto::PixelFormat pixel_format;
  uint8_t bits_per_pixel;

  fbl::Vector<UsbVideoFrameDesc> frame_descs;
  uint8_t default_frame_index;
};

// For changing characteristics of a video streaming interface and its
// underlying isochronous endpoint.
struct UsbVideoStreamingSetting {
  int alt_setting;

  uint8_t transactions_per_microframe;
  uint16_t max_packet_size;

  // USB_ENDPOINT_BULK or USB_ENDPOINT_ISOCHRONOUS
  int ep_type;
};

inline uint32_t setting_bandwidth(const UsbVideoStreamingSetting& setting) {
  return setting.max_packet_size * setting.transactions_per_microframe;
}

}  // namespace usb
}  // namespace video
