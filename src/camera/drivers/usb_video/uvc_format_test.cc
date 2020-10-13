// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "uvc_format.h"

#include <ddk/binding.h>
#include <zxtest/zxtest.h>

namespace video {
namespace usb {

// 8 bits for each RGB.
static constexpr uint32_t NANOSECS_IN_SEC = 1e9;

TEST(UvcFormat, ToFidl) {
  auto CheckFormat = [](const UvcFormat& usb, const fuchsia::camera::VideoFormat& fidl) {
    ASSERT_EQ(fidl.format.width, usb.width);
    ASSERT_EQ(fidl.format.height, usb.height);
    ASSERT_EQ(fidl.format.layers, 1);
    ASSERT_EQ(fidl.rate.frames_per_sec_numerator, NANOSECS_IN_SEC / 100);
    ASSERT_EQ(fidl.format.planes[0].bytes_per_row, usb.stride);
    ASSERT_EQ(fidl.rate.frames_per_sec_denominator, usb.default_frame_interval);
    switch (usb.pixel_format) {
      case UvcPixelFormat::BGRA32:
        ASSERT_EQ(fidl.format.pixel_format.type, fuchsia::sysmem::PixelFormatType::BGRA32);
        break;
      case UvcPixelFormat::I420:
        ASSERT_EQ(fidl.format.pixel_format.type, fuchsia::sysmem::PixelFormatType::I420);
        break;
      case UvcPixelFormat::M420:
        ASSERT_EQ(fidl.format.pixel_format.type, fuchsia::sysmem::PixelFormatType::M420);
        break;
      case UvcPixelFormat::NV12:
        ASSERT_EQ(fidl.format.pixel_format.type, fuchsia::sysmem::PixelFormatType::NV12);
        break;
      case UvcPixelFormat::YUY2:
        ASSERT_EQ(fidl.format.pixel_format.type, fuchsia::sysmem::PixelFormatType::YUY2);
        break;
      case UvcPixelFormat::MJPEG:
        ASSERT_EQ(fidl.format.pixel_format.type, fuchsia::sysmem::PixelFormatType::MJPEG);
        break;
      default:
        ASSERT_EQ(fidl.format.pixel_format.type, fuchsia::sysmem::PixelFormatType::INVALID);
    }
  };
  {
    UvcFormat format;
    format.width = 28;
    format.height = -80;
    format.default_frame_interval = 70;
    format.stride = 32;
    format.pixel_format = UvcPixelFormat::BGRA32;
    auto fidl = ToFidl(format);
    CheckFormat(format, fidl);
  }
  {
    UvcFormat format;
    format.width = 98;
    format.height = 20;
    format.default_frame_interval = 10;
    format.stride = 9001;
    format.pixel_format = UvcPixelFormat::I420;
    auto fidl = ToFidl(format);
    CheckFormat(format, fidl);
  }
  {
    UvcFormat format;
    format.width = 60;
    format.height = 120;
    format.default_frame_interval = 8;
    format.stride = -10;
    format.pixel_format = UvcPixelFormat::M420;
    auto fidl = ToFidl(format);
    CheckFormat(format, fidl);
  }
  {
    UvcFormat format;
    format.width = 17;
    format.height = 65534;
    format.default_frame_interval = 16;
    format.stride = 1;
    format.pixel_format = UvcPixelFormat::NV12;
    auto fidl = ToFidl(format);
    CheckFormat(format, fidl);
  }
  {
    UvcFormat format;
    format.width = 17;
    format.height = 65534;
    format.default_frame_interval = 16;
    format.stride = 1;
    format.pixel_format = UvcPixelFormat::YUY2;
    auto fidl = ToFidl(format);
    CheckFormat(format, fidl);
  }
  {
    UvcFormat format;
    format.width = 117;
    format.height = 60534;
    format.default_frame_interval = 16;
    format.stride = 1;
    format.pixel_format = UvcPixelFormat::MJPEG;
    auto fidl = ToFidl(format);
    CheckFormat(format, fidl);
  }
  {
    UvcFormat format;
    format.width = 17;
    format.height = 80;
    format.default_frame_interval = 16;
    format.stride = 1;
    format.pixel_format = UvcPixelFormat::INVALID;
    auto fidl = ToFidl(format);
    CheckFormat(format, fidl);
  }
}

TEST(UvcFormat, Compare) {
  {
    UvcFormat format;
    format.width = 117;
    format.height = 60534;
    format.default_frame_interval = 16;
    format.stride = 1;
    format.pixel_format = UvcPixelFormat::MJPEG;
    auto fidl = ToFidl(format);
    ASSERT_TRUE(Compare(fidl, format));
    format.stride = 2;
    ASSERT_FALSE(Compare(fidl, format));
    format.stride = 1;
    format.width = 201;
    ASSERT_FALSE(Compare(fidl, format));
    format.width = 117;
    format.pixel_format = NV12;
    ASSERT_FALSE(Compare(fidl, format));
  }
}

TEST(UvcFormat, ParseUsbDescriptor) {
  {
    UvcFormatList list;
    struct {
      usb_video_vs_uncompressed_format_desc fmt;
      usb_video_vs_frame_desc vc_desc;
    } __PACKED desc;
    desc.fmt.bLength = sizeof(desc.fmt);
    desc.fmt.bDescriptorSubType = USB_VIDEO_VS_FORMAT_UNCOMPRESSED;
    desc.fmt.bFormatIndex = 5;
    uint8_t guid[] = USB_VIDEO_GUID_YUY2_VALUE;
    memcpy(desc.fmt.guidFormat, guid, 16);
    desc.fmt.bBitsPerPixel = 12;
    desc.fmt.bNumFrameDescriptors = 1;
    desc.fmt.bDefaultFrameIndex = 2;
    desc.vc_desc.bDescriptorType = USB_VIDEO_CS_INTERFACE;
    desc.vc_desc.bLength = sizeof(desc.vc_desc);
    desc.vc_desc.bDescriptorSubType = USB_VIDEO_VS_FRAME_UNCOMPRESSED;
    desc.vc_desc.dwDefaultFrameInterval = 30;
    desc.vc_desc.wWidth = 90;
    desc.vc_desc.wHeight = 80;
    usb_desc_iter_t iter;
    iter.current = reinterpret_cast<uint8_t*>(&desc.vc_desc);
    iter.desc_end = iter.current + sizeof(desc.vc_desc);
    ASSERT_OK(list.ParseUsbDescriptor(reinterpret_cast<usb_video_vc_desc_header*>(&desc), &iter));
  }
  {
    UvcFormatList list;
    struct {
      usb_video_vs_uncompressed_format_desc fmt;
      usb_video_vs_frame_desc vc_desc;
    } __PACKED desc;
    desc.fmt.bLength = sizeof(desc.fmt);
    desc.fmt.bDescriptorSubType = USB_VIDEO_VS_FORMAT_UNCOMPRESSED;
    desc.fmt.bFormatIndex = 5;
    uint8_t guid[] = USB_VIDEO_GUID_YUY2_VALUE;
    memcpy(desc.fmt.guidFormat, guid, 16);
    desc.fmt.bBitsPerPixel = 12;
    desc.fmt.bNumFrameDescriptors = 1;
    desc.fmt.bDefaultFrameIndex = 2;
    desc.vc_desc.bDescriptorType = USB_VIDEO_CS_INTERFACE;
    desc.vc_desc.bLength = sizeof(desc.vc_desc);
    desc.vc_desc.bDescriptorSubType = USB_VIDEO_VS_FRAME_UNCOMPRESSED;
    desc.vc_desc.dwDefaultFrameInterval = 30;
    desc.vc_desc.wWidth = 90;
    desc.vc_desc.wHeight = 0;
    usb_desc_iter_t iter;
    iter.current = reinterpret_cast<uint8_t*>(&desc.vc_desc);
    iter.desc_end = iter.current + sizeof(desc.vc_desc);
    ASSERT_EQ(list.ParseUsbDescriptor(reinterpret_cast<usb_video_vc_desc_header*>(&desc), &iter),
              ZX_ERR_INVALID_ARGS);
  }
}

}  // namespace usb
}  // namespace video
