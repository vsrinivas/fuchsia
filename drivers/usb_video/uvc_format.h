// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_DRIVERS_USB_VIDEO_UVC_FORMAT_H_
#define GARNET_DRIVERS_USB_VIDEO_UVC_FORMAT_H_

#include <ddk/device.h>
#include <ddk/usb/usb.h>
#include <fbl/vector.h>
#include <fuchsia/camera/driver/cpp/fidl.h>
#include <zircon/compiler.h>
#include <zircon/hw/usb-video.h>
#include <zircon/hw/usb.h>

namespace video {
namespace usb {

// Decoded video dimensions and other frame-specific characteristics
// supported by frame-based formats.
struct UvcFrameDesc {
  uint8_t index;

  // Specified in 100ns units.
  uint32_t default_frame_interval;
  uint16_t width;
  uint16_t height;
  // The number of bytes per line of video.
  uint32_t stride;
};

enum UvcPixelFormat {
  INVALID,  // default value, not supported
  BGRA32,    // 32bpp BGRA, 1 plane.
  I420,
  M420,
  NV12,
  YUY2,
  MJPEG
};

// This is a flattened structure.  Instead of having a
// UvcFormat which has a vector of UvcFrameDesc, we create one
// UvcFormat for each framedesc.
struct UvcFormat {
  uint8_t format_index;
  UvcPixelFormat pixel_format;
  uint8_t bits_per_pixel;

  // Frame description
  uint8_t frame_index;

  // Specified in 100ns units.
  uint32_t default_frame_interval;
  uint16_t width;
  uint16_t height;
  // The number of bytes per line of video.
  uint32_t stride;
  uint8_t default_frame_index;
};

fuchsia::camera::driver::VideoFormat ToFidl(const UvcFormat &format_in);

bool Compare(const fuchsia::camera::driver::VideoFormat &vf,
             const UvcFormat &uf);

class UvcFormatList {
 public:
  size_t Size() { return formats_.size(); }
  zx_status_t ParseUsbDescriptor(usb_video_vc_desc_header *format_desc,
                                 usb_desc_iter_t *iter);
  uint32_t number_of_formats() { return number_of_formats_; }

  bool MatchFormat(const fuchsia::camera::driver::VideoFormat &requested_format,
                   uint8_t *format_index, uint8_t *frame_index,
                   uint32_t *default_frame_interval) const {
    for (const auto &format : formats_) {
      if (Compare(requested_format, format)) {
        *format_index = format.format_index;
        *frame_index = format.frame_index;
        *default_frame_interval = format.default_frame_interval;
        return true;
      }
    }
    return false;
  }

  void FillFormats(
      fidl::VectorPtr<fuchsia::camera::driver::VideoFormat> &formats) const {
    for (auto &format : formats_) {
      formats.push_back(ToFidl(format));
    }
  }

  // Make sure we reset number_of_formats_ when this class is moved out.
  UvcFormatList(UvcFormatList &&other) {
    formats_ = std::move(other.formats_);
    number_of_formats_ = other.number_of_formats_;
    other.number_of_formats_ = 0;
  }
  UvcFormatList() = default;

 private:
  uint32_t number_of_formats_ = 0;
  fbl::Vector<UvcFormat> formats_;
};

}  // namespace usb
}  // namespace video

#endif  // GARNET_DRIVERS_USB_VIDEO_UVC_FORMAT_H_
