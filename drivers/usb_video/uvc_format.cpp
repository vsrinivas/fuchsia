// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <ddk/debug.h>
#include <ddk/protocol/usb.h>
#include <ddk/usb/usb.h>
#include <fbl/vector.h>
#include <stdlib.h>
#include <zircon/device/usb.h>
#include <zircon/hw/usb-video.h>

#include "garnet/drivers/usb_video/usb-video-stream.h"
#include "garnet/drivers/usb_video/uvc_format.h"

namespace video {
namespace usb {

// 8 bits for each RGB.
static constexpr uint32_t MJPEG_BITS_PER_PIXEL = 24;
static constexpr uint32_t NANOSECS_IN_SEC = 1e9;

UvcPixelFormat guid_to_pixel_format(uint8_t guid[GUID_LENGTH]) {
  struct {
    uint8_t guid[GUID_LENGTH];
    UvcPixelFormat pixel_format;
  } GUID_LUT[] = {
      {USB_VIDEO_GUID_YUY2_VALUE, UvcPixelFormat::YUY2},
      {USB_VIDEO_GUID_NV12_VALUE, UvcPixelFormat::NV12},
      {USB_VIDEO_GUID_M420_VALUE, UvcPixelFormat::M420},
      {USB_VIDEO_GUID_I420_VALUE, UvcPixelFormat::I420},
  };

  for (const auto& g : GUID_LUT) {
    if (memcmp(g.guid, guid, GUID_LENGTH) == 0) {
      return g.pixel_format;
    }
  }
  return UvcPixelFormat::INVALID;
}

fuchsia::camera::driver::VideoFormat ToFidl(const UvcFormat& format_in) {
  fuchsia::camera::driver::VideoFormat ret = {
      .format.width = format_in.width,
      .format.height = format_in.height,
      .format.layers = 1,
      .format.bytes_per_row = format_in.stride,
      // The frame descriptor frame interval is expressed in 100ns units.
      // e.g. a frame interval of 333333 is equivalent to 30fps (1e7 / 333333).
      .rate.frames_per_sec_numerator =
          NANOSECS_IN_SEC / 100,  // static_cast<uint32_t>(1e7),
      .rate.frames_per_sec_denominator = format_in.default_frame_interval};
  // Convert Pixel Format:
  switch (format_in.pixel_format) {
    case UvcPixelFormat::BGRA32:
      ret.format.pixel_format.type = fuchsia::sysmem::PixelFormatType::BGRA32;
      break;
    case UvcPixelFormat::I420:
      ret.format.pixel_format.type = fuchsia::sysmem::PixelFormatType::I420;
      break;
    case UvcPixelFormat::M420:
      ret.format.pixel_format.type = fuchsia::sysmem::PixelFormatType::M420;
      break;
    case UvcPixelFormat::NV12:
      ret.format.pixel_format.type = fuchsia::sysmem::PixelFormatType::NV12;
      break;
    case UvcPixelFormat::YUY2:
      ret.format.pixel_format.type = fuchsia::sysmem::PixelFormatType::YUY2;
      break;
    case UvcPixelFormat::MJPEG:
      ret.format.pixel_format.type = fuchsia::sysmem::PixelFormatType::MJPEG;
      break;
    default:
      ret.format.pixel_format.type = fuchsia::sysmem::PixelFormatType::INVALID;
  }
  return ret;
}

bool Compare(const fuchsia::camera::driver::VideoFormat& vf,
             const UvcFormat& uf) {
  fuchsia::camera::driver::VideoFormat uvf = ToFidl(uf);

  bool has_equal_frame_rate =
      (static_cast<uint64_t>(vf.rate.frames_per_sec_numerator) *
       uvf.rate.frames_per_sec_denominator) ==
      (static_cast<uint64_t>(vf.rate.frames_per_sec_numerator) *
       uvf.rate.frames_per_sec_denominator);

  if (vf.format.pixel_format == uvf.format.pixel_format &&
      vf.format.width == uvf.format.width &&
      vf.format.height == uvf.format.height &&
      vf.format.bytes_per_row == uvf.format.bytes_per_row &&
      has_equal_frame_rate) {
    return true;
  }
  return false;
}

// Parses the payload format descriptor and any corresponding frame descriptors.
// The result is stored in out_format.
zx_status_t UvcFormatList::ParseUsbDescriptor(
    usb_video_vc_desc_header* format_desc, usb_desc_iter_t* iter) {
  uint8_t want_frame_type = 0;
  int want_num_frame_descs = 0;

  // These fields are shared between all frame descriptions:
  uint8_t format_index, default_frame_index;
  UvcPixelFormat pixel_format;
  uint8_t bits_per_pixel;

  switch (format_desc->bDescriptorSubtype) {
    case USB_VIDEO_VS_FORMAT_UNCOMPRESSED: {
      usb_video_vs_uncompressed_format_desc* uncompressed_desc =
          (usb_video_vs_uncompressed_format_desc*)format_desc;
      zxlogf(TRACE,
             "USB_VIDEO_VS_FORMAT_UNCOMPRESSED bNumFrameDescriptors %u "
             "bBitsPerPixel %u\n",
             uncompressed_desc->bNumFrameDescriptors,
             uncompressed_desc->bBitsPerPixel);

      want_frame_type = USB_VIDEO_VS_FRAME_UNCOMPRESSED;
      format_index = uncompressed_desc->bFormatIndex;
      pixel_format = guid_to_pixel_format(uncompressed_desc->guidFormat);
      bits_per_pixel = uncompressed_desc->bBitsPerPixel;
      want_num_frame_descs = uncompressed_desc->bNumFrameDescriptors;
      default_frame_index = uncompressed_desc->bDefaultFrameIndex;
      break;
    }
    case USB_VIDEO_VS_FORMAT_MJPEG: {
      usb_video_vs_mjpeg_format_desc* mjpeg_desc =
          (usb_video_vs_mjpeg_format_desc*)format_desc;
      zxlogf(TRACE,
             "USB_VIDEO_VS_FORMAT_MJPEG bNumFrameDescriptors %u bmFlags %d\n",
             mjpeg_desc->bNumFrameDescriptors, mjpeg_desc->bmFlags);

      want_frame_type = USB_VIDEO_VS_FRAME_MJPEG;
      format_index = mjpeg_desc->bFormatIndex;
      pixel_format = UvcPixelFormat::MJPEG;
      bits_per_pixel = MJPEG_BITS_PER_PIXEL;
      want_num_frame_descs = mjpeg_desc->bNumFrameDescriptors;
      default_frame_index = mjpeg_desc->bDefaultFrameIndex;
      break;
    }
    // TODO(jocelyndang): handle other formats.
    default:
      zxlogf(ERROR, "unsupported format bDescriptorSubtype %u\n",
             format_desc->bDescriptorSubtype);
      return ZX_ERR_NOT_SUPPORTED;
  }

  fbl::AllocChecker ac;
  // TODO(garratt): add case for format with no frame_desc
  formats_.reserve(formats_.size() + want_num_frame_descs, &ac);
  if (!ac.check()) {
    return ZX_ERR_NO_MEMORY;
  }

  // The format descriptor mut be immediately followed by its frame descriptors,
  // if any.
  int num_frame_descs_found = 0;

  usb_descriptor_header_t* header;
  while ((header = usb_desc_iter_peek(iter)) != NULL &&
         header->bDescriptorType == USB_VIDEO_CS_INTERFACE &&
         num_frame_descs_found < want_num_frame_descs) {
    usb_video_vc_desc_header* format_desc = (usb_video_vc_desc_header*)header;
    if (format_desc->bDescriptorSubtype != want_frame_type) {
      break;
    }

    switch (format_desc->bDescriptorSubtype) {
      case USB_VIDEO_VS_FRAME_UNCOMPRESSED:
      case USB_VIDEO_VS_FRAME_MJPEG: {
        usb_video_vs_frame_desc* desc = (usb_video_vs_frame_desc*)header;

        // Intervals are specified in 100 ns units.
        double framesPerSec = 1 / (desc->dwDefaultFrameInterval * 100 / 1e9);
        zxlogf(
            TRACE, "%s (%u x %u) %.2f frames / sec\n",
            format_desc->bDescriptorSubtype == USB_VIDEO_VS_FRAME_UNCOMPRESSED
                ? "USB_VIDEO_VS_FRAME_UNCOMPRESSED"
                : "USB_VIDEO_VS_FRAME_MJPEG",
            desc->wWidth, desc->wHeight, framesPerSec);

        video::usb::UvcFormat frame_desc = {
            .format_index = format_index,
            .pixel_format = pixel_format,
            .bits_per_pixel = bits_per_pixel,
            .default_frame_index = default_frame_index,
            .frame_index = desc->bFrameIndex,
            .default_frame_interval = desc->dwDefaultFrameInterval,
            .width = desc->wWidth,
            .height = desc->wHeight,
            .stride = desc->dwMaxVideoFrameBufferSize / desc->wHeight};
        formats_.push_back(frame_desc);
      } break;
      default:
        zxlogf(ERROR, "unhandled frame type: %u\n",
               format_desc->bDescriptorSubtype);
        return ZX_ERR_NOT_SUPPORTED;
    }
    header = usb_desc_iter_next(iter);
    num_frame_descs_found++;
  }
  if (num_frame_descs_found != want_num_frame_descs) {
    zxlogf(ERROR, "missing %u frame descriptors\n",
           want_num_frame_descs - num_frame_descs_found);
    return ZX_ERR_INTERNAL;
  }
  // TODO(jocelyndang); parse still image frame and color matching descriptors.
  return ZX_OK;
}

}  // namespace usb
}  // namespace video
