// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_CAMERA_DRIVERS_USB_VIDEO_USB_VIDEO_H_
#define SRC_CAMERA_DRIVERS_USB_VIDEO_USB_VIDEO_H_

#include <fuchsia/camera/cpp/fidl.h>
#include <zircon/compiler.h>
#include <zircon/hw/usb.h>
#include <zircon/hw/usb/video.h>

#include <ddk/device.h>
#include <fbl/vector.h>
#include <usb/usb.h>

#include "src/camera/drivers/usb_video/uvc_format.h"

namespace video {
namespace usb {

// For changing characteristics of a video streaming interface and its
// underlying isochronous endpoint.
struct UsbVideoStreamingSetting {
  int alt_setting;

  uint8_t transactions_per_microframe;
  uint16_t max_packet_size;

  // USB_ENDPOINT_BULK or USB_ENDPOINT_ISOCHRONOUS
  int ep_type;
};

// Information about the vendor and product that can be gleaned from the USB
// descriptor.
struct UsbDeviceInfo {
  uint16_t vendor_id;
  uint16_t product_id;
  std::string manufacturer;
  std::string product_name;
  std::string serial_number;
};

inline uint32_t setting_bandwidth(const UsbVideoStreamingSetting& setting) {
  return setting.max_packet_size * setting.transactions_per_microframe;
}

}  // namespace usb
}  // namespace video

#endif  // SRC_CAMERA_DRIVERS_USB_VIDEO_USB_VIDEO_H_
