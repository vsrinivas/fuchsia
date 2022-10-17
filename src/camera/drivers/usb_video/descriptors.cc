// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/camera/drivers/usb_video/descriptors.h"

#include <endian.h>
#include <lib/affine/ratio.h>
#include <lib/ddk/debug.h>
#include <stdlib.h>

#include <map>
#include <set>
#include <vector>

namespace camera::usb_video {

namespace {
static constexpr uint32_t MJPEG_BITS_PER_PIXEL = 24;
static constexpr uint32_t NANOSECS_IN_SEC = 1e9;

UvcPixelFormat guid_to_pixel_format(const uint8_t guid[GUID_LENGTH]) {
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

std::map<int, std::string> desc2string = {
    {0x01, "USB_DT_DEVICE"},
    {0x02, "USB_DT_CONFIG"},
    {0x03, "USB_DT_STRING"},
    {0x04, "USB_DT_INTERFACE"},
    {0x05, "USB_DT_ENDPOINT"},
    {0x06, "USB_DT_DEVICE_QUALIFIER"},
    {0x07, "USB_DT_OTHER_SPEED_CONFIG"},
    {0x08, "USB_DT_INTERFACE_POWER"},
    {0x0b, "USB_DT_INTERFACE_ASSOCIATION"},
    {0x21, "USB_DT_HID"},
    {0x22, "USB_DT_HIDREPORT"},
    {0x23, "USB_DT_HIDPHYSICAL"},
    {0x24, "USB_DT_CS_INTERFACE"},
    {0x25, "USB_DT_CS_ENDPOINT"},
    {0x30, "USB_DT_SS_EP_COMPANION"},
    {0x31, "USB_DT_SS_ISOCH_EP_COMPANION"},
};

std::string DType2String(uint8_t type) {
  auto iter = desc2string.find(type);
  if (iter == desc2string.end()) {
    return "Invalid Descriptor Type";
  }
  return iter->second;
}

const std::set<uint8_t> kFormatSubtypes = {
    USB_VIDEO_VS_FORMAT_UNCOMPRESSED, USB_VIDEO_VS_FORMAT_MJPEG,
    USB_VIDEO_VS_FORMAT_MPEG2TS,      USB_VIDEO_VS_FORMAT_DV,
    USB_VIDEO_VS_FORMAT_FRAME_BASED,  USB_VIDEO_VS_FORMAT_STREAM_BASED,
    USB_VIDEO_VS_FORMAT_H264,         USB_VIDEO_VS_FORMAT_H264_SIMULCAST,
    USB_VIDEO_VS_FORMAT_VP8,          USB_VIDEO_VS_FORMAT_VP8_SIMULCAST};

const std::set<uint8_t> kFrameSubtypes = {USB_VIDEO_VS_FRAME_UNCOMPRESSED, USB_VIDEO_VS_FRAME_MJPEG,
                                          USB_VIDEO_VS_FRAME_FRAME_BASED, USB_VIDEO_VS_FRAME_H264,
                                          USB_VIDEO_VS_FRAME_VP8};

// Which frame types are allowed for each format.
// For each format, only one frame type is allowed.
const std::map<uint8_t, uint8_t> kAllowedFrames = {
    {USB_VIDEO_VS_FORMAT_UNCOMPRESSED, USB_VIDEO_VS_FRAME_UNCOMPRESSED},
    {USB_VIDEO_VS_FORMAT_MJPEG, USB_VIDEO_VS_FRAME_MJPEG},
    {USB_VIDEO_VS_FORMAT_MPEG2TS, USB_VIDEO_VS_FRAME_MJPEG},
    {USB_VIDEO_VS_FORMAT_DV, USB_VIDEO_VS_FRAME_MJPEG},
    {USB_VIDEO_VS_FORMAT_FRAME_BASED, USB_VIDEO_VS_FRAME_FRAME_BASED},
    {USB_VIDEO_VS_FORMAT_STREAM_BASED, USB_VIDEO_VS_FRAME_FRAME_BASED},
    {USB_VIDEO_VS_FORMAT_H264, USB_VIDEO_VS_FRAME_H264},
    {USB_VIDEO_VS_FORMAT_H264_SIMULCAST, USB_VIDEO_VS_FRAME_H264},
    {USB_VIDEO_VS_FORMAT_VP8, USB_VIDEO_VS_FRAME_VP8},
    {USB_VIDEO_VS_FORMAT_VP8_SIMULCAST, USB_VIDEO_VS_FRAME_VP8}};

std::map<UvcPixelFormat, fuchsia::sysmem::PixelFormatType> Uvc2SysmemPixelFormat = {
    {UvcPixelFormat::BGRA32, fuchsia::sysmem::PixelFormatType::BGRA32},
    {UvcPixelFormat::I420, fuchsia::sysmem::PixelFormatType::I420},
    {UvcPixelFormat::M420, fuchsia::sysmem::PixelFormatType::M420},
    {UvcPixelFormat::NV12, fuchsia::sysmem::PixelFormatType::NV12},
    {UvcPixelFormat::YUY2, fuchsia::sysmem::PixelFormatType::YUY2},
    {UvcPixelFormat::MJPEG, fuchsia::sysmem::PixelFormatType::MJPEG}};

// Verifies that the iterator is valid, and that the
// memory it points to is valid for the full length given.
zx::result<uint8_t> VerifyDescriptor(usb_desc_iter_t* iter) {
  usb_descriptor_header_t* header = usb_desc_iter_peek(iter);
  if (header == NULL) {
    return zx::error(ZX_ERR_STOP);
  }

  if (usb_desc_iter_get_structure(iter, header->b_length) == NULL) {
    return zx::error(ZX_ERR_BAD_STATE);
  }
  return zx::ok(header->b_descriptor_type);
}

// Verify and extract arbitrary struct from the usb descriptor iterator.
template <class T>
zx::result<T> GetStruct(usb_desc_iter_t* iter, uint8_t required_type = 0) {
  auto type_or = VerifyDescriptor(iter);
  if (type_or.is_error()) {
    return type_or.take_error();
  }
  // Optionally match the descriptor type:
  if (required_type != 0 && *type_or != required_type) {
    zxlogf(DEBUG, "Expected %s descriptor type, got %s", DType2String(required_type).c_str(),
           DType2String(*type_or).c_str());
    return zx::error(ZX_ERR_NEXT);
  }

  // Verify that we can safely cast the blob data pointer to the given type, and then
  // return by value to copy out the data from the descriptor blob.
  if (void* data = usb_desc_iter_get_structure(iter, sizeof(T))) {
    return zx::ok(*reinterpret_cast<T*>(data));
  }
  return zx::error(ZX_ERR_BAD_STATE);
}

zx::result<usb_interface_descriptor_t> VerifyStdInterface(usb_desc_iter_t* iter) {
  return GetStruct<usb_interface_descriptor_t>(iter, USB_DT_INTERFACE);
}

zx::result<usb_cs_interface_descriptor_t> VerifyCSInterface(usb_desc_iter_t* iter) {
  return GetStruct<usb_cs_interface_descriptor_t>(iter, USB_DT_CS_INTERFACE);
}

zx::result<usb_endpoint_info_descriptor_t> VerifyEndpoint(usb_desc_iter_t* iter) {
  return GetStruct<usb_endpoint_info_descriptor_t>(iter, USB_DT_ENDPOINT);
}

bool IsVideoControlInterface(usb_desc_iter_t* iter) {
  auto header_or = VerifyStdInterface(iter);
  return header_or.is_ok() && header_or->b_interface_class == USB_CLASS_VIDEO &&
         header_or->b_interface_sub_class == USB_SUBCLASS_VIDEO_CONTROL;
}

bool IsVideoStreamingInterface(usb_desc_iter_t* iter) {
  auto header_or = VerifyStdInterface(iter);
  return header_or.is_ok() && header_or->b_interface_class == USB_CLASS_VIDEO &&
         header_or->b_interface_sub_class == USB_SUBCLASS_VIDEO_STREAMING;
}

zx::result<uint32_t> GetClockFrequency(usb_desc_iter_t* iter) {
  auto cs_descrip_or = VerifyCSInterface(iter);
  if (cs_descrip_or.is_error()) {
    return cs_descrip_or.take_error();
  }
  if (cs_descrip_or->b_descriptor_sub_type != USB_VIDEO_VC_HEADER) {
    return zx::error(ZX_ERR_BAD_STATE);
  }
  auto vc_header = reinterpret_cast<usb_video_vc_header_desc*>(
      usb_desc_iter_get_structure(iter, sizeof(usb_video_vc_header_desc)));
  if (vc_header == NULL) {
    return zx::error(ZX_ERR_BAD_STATE);
  }
  return zx::ok(vc_header->dwClockFrequency);
}

zx::result<usb_video_vs_input_header_desc_short> GetInputHeader(usb_desc_iter_t* iter) {
  auto cs_descrip_or = VerifyCSInterface(iter);
  if (cs_descrip_or.is_error()) {
    return cs_descrip_or.take_error();
  }
  if (cs_descrip_or->b_descriptor_sub_type != USB_VIDEO_VS_INPUT_HEADER) {
    return zx::error(ZX_ERR_BAD_STATE);
  }
  return GetStruct<usb_video_vs_input_header_desc_short>(iter);
}

// VerifyFormat returns the pointer to the data in the iterator blob.
// This is because the actual header is much longer, and is type specific to the
// format.  This returned pointer will later be re-cast to the full header type,
// after verifying that that the size of the type is less than the bLength field
// of the header.
zx::result<const usb_video_format_header*> VerifyFormat(usb_desc_iter_t* iter) {
  auto cs_descrip_or = VerifyCSInterface(iter);
  if (cs_descrip_or.is_error()) {
    return cs_descrip_or.take_error();
  }
  if (kFormatSubtypes.count(cs_descrip_or->b_descriptor_sub_type) == 0) {
    zxlogf(DEBUG, "Descriptor subtype 0x%x is not a format!", cs_descrip_or->b_descriptor_sub_type);
    return zx::error(ZX_ERR_WRONG_TYPE);
  }
  auto header = reinterpret_cast<usb_video_format_header*>(
      usb_desc_iter_get_structure(iter, sizeof(usb_video_format_header)));
  if (header == NULL) {
    return zx::error(ZX_ERR_BAD_STATE);
  }
  return zx::ok(header);
}

// VerifyFrame returns the pointer to the data in the iterator blob.
// This is because the actual header is much longer, and is type specific to the
// format.  This returned pointer will later be re-cast to the full header type,
// after verifying that that the size of the type is less than the bLength field
// of the header.
zx::result<const usb_video_frame_header*> VerifyFrame(usb_desc_iter_t* iter) {
  auto cs_descrip_or = VerifyCSInterface(iter);
  if (cs_descrip_or.is_error()) {
    return cs_descrip_or.take_error();
  }
  if (kFrameSubtypes.count(cs_descrip_or->b_descriptor_sub_type) == 0) {
    zxlogf(DEBUG, "Descriptor subtype 0x%x is not a frame!", cs_descrip_or->b_descriptor_sub_type);
    return zx::error(ZX_ERR_WRONG_TYPE);
  }
  auto header = reinterpret_cast<usb_video_frame_header*>(
      usb_desc_iter_get_structure(iter, sizeof(usb_video_frame_header)));
  if (header == NULL) {
    return zx::error(ZX_ERR_BAD_STATE);
  }
  return zx::ok(header);
}

// ParseUvcFormat is passed two pointers to the data in the usb descriptor blob.
// Each pointer is cast to a struct that represents the first few bytes of
// a longer, format specific struct. The pointer has already been checked to be valid
// out to pointer[0] bytes, so casting to any struct whose length is less than
// pointer[0] is guaranteed to be operating in a valid memory space.
// The UVC spec dictates the format of the struct based on the bDescriptorSubType
// field of the header.
// The memory is assumed to be valid for the duration of this function.
// Other than checking to prevent divide-by-zero errors, we don't validate the
// frame parameters. None of the frame parameters affect the operation of this driver; this
// driver simply relays the reported frame parameters to a client program so the client
// can select the desired frame configuration.
// TODO(fxbug.dev/104233): Use of dwMaxVideoFrameBufferSize has been deprecated.  The
// dwMaxVideoFrameSize field obtained from the Video Probe and Commit control exchange
// should be used instead.
zx::result<UvcFormat> ParseUvcFormat(const usb_video_format_header* format,
                                     const usb_video_frame_header* frame) {
  // make sure the format type and frame type match:
  auto iter = kAllowedFrames.find(format->bDescriptorSubType);
  if (iter == kAllowedFrames.end()) {
    zxlogf(ERROR, "Unsupported frame type");
    return zx::error(ZX_ERR_NOT_SUPPORTED);
  }

  if (iter->second != frame->bDescriptorSubType) {
    zxlogf(ERROR, "Mismatched format and frame types");
    return zx::error(ZX_ERR_BAD_STATE);
  }

  switch (format->bDescriptorSubType) {
    case USB_VIDEO_VS_FORMAT_UNCOMPRESSED: {
      if (format->bLength < sizeof(usb_video_vs_uncompressed_format_desc) ||
          frame->bLength < sizeof(usb_video_vs_frame_desc)) {
        return zx::error(ZX_ERR_BAD_STATE);
      }
      auto format_desc = reinterpret_cast<const usb_video_vs_uncompressed_format_desc*>(format);
      auto frame_desc = reinterpret_cast<const usb_video_vs_frame_desc*>(frame);
      // Prevent divide by zero error from bad data:
      if (frame_desc->wHeight == 0) {
        zxlogf(ERROR, "Frame description claims image height == 0!");
        return zx::error(ZX_ERR_BAD_STATE);
      }
      return zx::ok(UvcFormat{.format_index = format_desc->bFormatIndex,
                              .frame_index = frame_desc->bFrameIndex,
                              .pixel_format = guid_to_pixel_format(format_desc->guidFormat),
                              .bits_per_pixel = format_desc->bBitsPerPixel,
                              .default_frame_interval = frame_desc->dwDefaultFrameInterval,
                              .width = frame_desc->wWidth,
                              .height = frame_desc->wHeight,
                              .stride = frame_desc->dwMaxVideoFrameBufferSize / frame_desc->wHeight,
                              .default_frame_index = format_desc->bDefaultFrameIndex});
    }
    case USB_VIDEO_VS_FORMAT_MJPEG: {
      if (format->bLength < sizeof(usb_video_vs_mjpeg_format_desc) ||
          frame->bLength < sizeof(usb_video_vs_frame_desc)) {
        return zx::error(ZX_ERR_BAD_STATE);
      }
      auto format_desc = reinterpret_cast<const usb_video_vs_mjpeg_format_desc*>(format);
      auto frame_desc = reinterpret_cast<const usb_video_vs_frame_desc*>(frame);
      // Prevent divide by zero error from bad data:
      if (frame_desc->wHeight == 0) {
        zxlogf(ERROR, "Frame description claims image height == 0!");
        return zx::error(ZX_ERR_BAD_STATE);
      }
      return zx::ok(UvcFormat{.format_index = format_desc->bFormatIndex,
                              .frame_index = frame_desc->bFrameIndex,
                              .pixel_format = UvcPixelFormat::MJPEG,
                              .bits_per_pixel = MJPEG_BITS_PER_PIXEL,
                              .default_frame_interval = frame_desc->dwDefaultFrameInterval,
                              .width = frame_desc->wWidth,
                              .height = frame_desc->wHeight,
                              .stride = frame_desc->dwMaxVideoFrameBufferSize / frame_desc->wHeight,
                              .default_frame_index = format_desc->bDefaultFrameIndex});
    }

    case USB_VIDEO_VS_FORMAT_FRAME_BASED: {
      if (format->bLength < sizeof(usb_video_vs_frame_based_format_desc) ||
          frame->bLength < sizeof(usb_video_vs_frame_based_frame_desc)) {
        return zx::error(ZX_ERR_BAD_STATE);
      }
      auto format_desc = reinterpret_cast<const usb_video_vs_frame_based_format_desc*>(format);
      auto frame_desc = reinterpret_cast<const usb_video_vs_frame_based_frame_desc*>(frame);
      return zx::ok(UvcFormat{.format_index = format_desc->bFormatIndex,
                              .frame_index = frame_desc->bFrameIndex,
                              .pixel_format = guid_to_pixel_format(format_desc->guidFormat),
                              .bits_per_pixel = format_desc->bBitsPerPixel,
                              .default_frame_interval = frame_desc->dwDefaultFrameInterval,
                              .width = frame_desc->wWidth,
                              .height = frame_desc->wHeight,
                              .stride = frame_desc->dwBytesPerLine,
                              .default_frame_index = format_desc->bDefaultFrameIndex});
    }
    default:
      zxlogf(ERROR, "unsupported format bDescriptorSubtype %u", format->bDescriptorSubType);
  }
  return zx::error(ZX_ERR_NOT_SUPPORTED);
}

zx::result<std::vector<UvcFormat>> GetFormat(usb_desc_iter_t* iter) {
  std::vector<UvcFormat> formats;
  zx::result<const usb_video_format_header*> format_header_or = VerifyFormat(iter);
  if (format_header_or.is_error()) {
    return format_header_or.take_error();
  }
  usb_desc_iter_advance(iter);
  // Now load the frames that come after the format. we should have bNumFrameDescriptors
  for (uint8_t i = 0; i < format_header_or.value()->bNumFrameDescriptors; ++i) {
    zx::result<const usb_video_frame_header*> frame_header_or = VerifyFrame(iter);
    if (frame_header_or.is_error()) {
      return frame_header_or.take_error();
    }
    auto uvc_format_or = ParseUvcFormat(*format_header_or, *frame_header_or);
    if (uvc_format_or.is_error()) {
      return uvc_format_or.take_error();
    }
    formats.push_back(*uvc_format_or);
    usb_desc_iter_advance(iter);
  }
  // Optionally we may have still image frame or color matching descriptors:
  // UVC Spec. 3.9.2.5:
  // The Still Image Frame descriptor is only applicable for a VS interface that supports
  // method 2 or 3 of still image capture in conjunction with frame-based Payload formats
  // (e.g., MJPEG, uncompressed, etc.). The Still Image Frame descriptor defines the
  // characteristics of the still image capture for these frame-based formats.
  // A single still Image Frame descriptor follows the Frame descriptor(s) for
  // each Format descriptor group.
  // UVC Spec. 3.9.2.6:
  // The Color Matching descriptor is an optional descriptor used to describe the
  // color profile of the video data in an unambiguous way. Only one instance is allowed
  // for a given format and if present, the Color Matching descriptor shall be placed
  // following the Video and Still Image Frame descriptors for that format.
  auto cs_descrip_or = VerifyCSInterface(iter);
  while (cs_descrip_or.is_ok() &&
         (cs_descrip_or->b_descriptor_sub_type == USB_VIDEO_VS_STILL_IMAGE_FRAME ||
          cs_descrip_or->b_descriptor_sub_type == USB_VIDEO_VS_COLORFORMAT)) {
    usb_desc_iter_advance(iter);
    cs_descrip_or = VerifyCSInterface(iter);
  }
  return zx::ok(formats);
}

// From section 9.6.6 of the Universal Serial Bus Specification Revision 2.0:
// in regards to wMaxPacketSize in table 9-13:
//   For all endpoints, bits 10..0 specify the maximum packet size (in bytes).
//   For high-speed isochronous and interrupt endpoints:
//   Bits 12..11 specify the number of additional transaction opportunities per microframe:
//     00 = None (1 transaction per microframe)
//     01 = 1 additional (2 per microframe)
//     10 = 2 additional (3 per microframe)
//     11 = Reserved
// Note that for super speed devices, additional descriptors need to be parsed.
uint32_t UsbEPIsocBandwidth(const usb_endpoint_info_descriptor_t& ep) {
  uint16_t wMaxPacketSize = static_cast<uint16_t>(le16toh((ep).w_max_packet_size));
  uint32_t transactions_per_microframe = ((wMaxPacketSize >> 11) & 3) + 1;
  uint32_t max_packet_size = wMaxPacketSize & 0x07FF;
  return transactions_per_microframe * max_packet_size;
}

// Alternate settings consist of a VideoStreaming Standard Interface
// followed by an endpoint descriptor.
zx::result<StreamingEndpointSetting> GetAlternateSetting(usb_desc_iter_t* iter) {
  if (!IsVideoStreamingInterface(iter)) {
    return zx::error(ZX_ERR_BAD_STATE);
  }
  // The VideoStreaming Standard Interface indicates the beginning of the streaming settings.
  auto vs_interface_or = GetStruct<usb_interface_info_descriptor_t>(iter);
  if (vs_interface_or.is_error()) {
    zxlogf(ERROR, "Failed to copy out streaming interface");
    return vs_interface_or.take_error();
  }
  usb_desc_iter_advance(iter);
  auto endpoint_or = VerifyEndpoint(iter);
  if (endpoint_or.is_error()) {
    return endpoint_or.take_error();
  }
  usb_desc_iter_advance(iter);
  return zx::ok(StreamingEndpointSetting{
      .address = endpoint_or->b_endpoint_address,
      .alt_setting = vs_interface_or->b_alternate_setting,
      .isoc_bandwidth = UsbEPIsocBandwidth(endpoint_or.value()),
      .ep_type = usb_ep_type(&(endpoint_or.value())),
  });
}

}  // anonymous namespace

zx::result<StreamingSetting> LoadStreamingSettings(usb_desc_iter_t* iter) {
  StreamingSetting settings;
  // Skip past the first few headers
  while (VerifyDescriptor(iter).is_ok() && !IsVideoControlInterface(iter)) {
    zxlogf(DEBUG, "Skipping interface to get to Control");
    usb_desc_iter_advance(iter);
  }
  // Check if we quit the while loop because of a bad iterator:
  if (!VerifyDescriptor(iter).is_ok()) {
    return zx::error(ZX_ERR_BAD_STATE);
  }
  // We don't actually need anything from the Standard VideoControl Interface header.
  // It mostly tells us that the following Class Specific (CS) interface headers relate
  // to control.
  usb_desc_iter_advance(iter);
  // Next we expect the Video Control header, then following the video control header
  // will be one or more Unit and/or Terminal Descriptors.
  // The control descriptors describe various camera controls such as zoom, gain etc.
  // More information about the control descriptors can be found in section 3.7 of the
  // USB Video Class specification.
  // Currently, the only value we take from the control descriptors is the clock frequency,
  // which we use for calculating frame timestamps.
  // Retrieve that frequency from the Video Control header:
  auto freq_or = GetClockFrequency(iter);
  if (freq_or.is_error()) {
    return freq_or.take_error();
  }
  settings.hw_clock_frequency = *freq_or;

  // Now, proceed to ignore the rest of the control descriptors and the interrupt descriptors
  // which follow that. We're going to skip all the way to the VideoStreaming Standard Interface
  // descriptor.
  while (VerifyDescriptor(iter).is_ok() && !IsVideoStreamingInterface(iter)) {
    zxlogf(DEBUG, "Skipping interface to get to Streaming interface");
    usb_desc_iter_advance(iter);
  }
  // Check if we quit the while loop because of a bad iterator:
  if (!VerifyDescriptor(iter).is_ok()) {
    return zx::error(ZX_ERR_BAD_STATE);
  }
  // The VideoStreaming Standard Interface indicates the beginning of the streaming settings.
  auto vs_interface_or = GetStruct<usb_interface_info_descriptor_t>(iter);
  if (vs_interface_or.is_error()) {
    zxlogf(ERROR, "Failed to copy out streaming interface");
    return vs_interface_or.take_error();
  }
  settings.vs_interface = *vs_interface_or;
  usb_desc_iter_advance(iter);
  // Next we expect the input header:
  auto input_header_or = GetInputHeader(iter);
  if (input_header_or.is_error()) {
    return input_header_or.take_error();
  }
  settings.input_header = *input_header_or;
  usb_desc_iter_advance(iter);
  // Now we expect a series of formats, totaling to settings.input_header.bNumFormats
  for (int i = 0; i < settings.input_header.bNumFormats; ++i) {
    // We expect GetFormat to advance the iterator before it returns.
    auto format_or = GetFormat(iter);
    if (format_or.is_error()) {
      return format_or.take_error();
    }
    for (auto& s : *format_or) {
      settings.formats.push_back(s);
    }
  }
  // Next are the alternate settings.  These indicate a different bandwidth, but do not
  // repeat the streaming formats.
  while (IsVideoStreamingInterface(iter)) {
    // GetAlternateSetting will advance the iterator.
    auto endpoint_setting_or = GetAlternateSetting(iter);
    if (endpoint_setting_or.is_error()) {
      return endpoint_setting_or.take_error();
    }
    settings.endpoint_settings.push_back(*endpoint_setting_or);
  }
  // Now check the full set of alternate settings:
  if (settings.endpoint_settings.size() == 0) {
    zxlogf(ERROR, "No alternate settings given");
    return zx::error(ZX_ERR_BAD_STATE);
  }
  if (settings.endpoint_settings[0].ep_type == USB_ENDPOINT_BULK) {
    if (settings.endpoint_settings.size() > 1 || settings.endpoint_settings[0].alt_setting != 0) {
      zxlogf(ERROR, "bulk endpoint shall support only alternate setting zero.");
      return zx::error(ZX_ERR_BAD_STATE);
    }
  } else {
    // For isochronous endpoints, just make sure all the types are consistent:
    for (const StreamingEndpointSetting& setting : settings.endpoint_settings) {
      // The streaming settings should all be of the same type,
      // in this case all USB_ENDPOINT_ISOCHRONOUS.
      if (setting.ep_type != USB_ENDPOINT_ISOCHRONOUS) {
        zxlogf(ERROR, "expected isochronous endpoint, got %u", setting.ep_type);
        return zx::error(ZX_ERR_BAD_STATE);
      }
    }
  }
  return zx::ok(std::move(settings));
}

fuchsia::camera::VideoFormat UvcFormat::ToFidl() const {
  fuchsia::camera::VideoFormat ret = {
      .format =
          {
              .width = width,
              .height = height,
              .layers = 1,
          },
      // The frame descriptor frame interval is expressed in 100ns units.
      // e.g. a frame interval of 333333 is equivalent to 30fps (1e7 / 333333).
      .rate = {.frames_per_sec_numerator = NANOSECS_IN_SEC / 100,
               .frames_per_sec_denominator = default_frame_interval},
  };
  ret.format.planes[0].bytes_per_row = stride;
  if (pixel_format == UvcPixelFormat::I420) {
    ret.format.layers = 3;
    ret.format.planes[1].bytes_per_row = stride / 2;
    ret.format.planes[2].bytes_per_row = stride / 2;
  }
  if (pixel_format == UvcPixelFormat::NV12) {
    ret.format.layers = 2;
    ret.format.planes[1].bytes_per_row = stride;
  }

  // Convert Pixel Format:
  ret.format.pixel_format.type = fuchsia::sysmem::PixelFormatType::INVALID;
  if (auto iter = Uvc2SysmemPixelFormat.find(pixel_format); iter != Uvc2SysmemPixelFormat.end()) {
    ret.format.pixel_format.type = iter->second;
  }
  return ret;
}

bool FrameRatesEqual(fuchsia::camera::FrameRate a, fuchsia::camera::FrameRate b) {
  affine::Ratio::Reduce(&a.frames_per_sec_numerator, &a.frames_per_sec_denominator);
  affine::Ratio::Reduce(&b.frames_per_sec_numerator, &b.frames_per_sec_denominator);
  return (a.frames_per_sec_numerator == b.frames_per_sec_numerator) &&
         (a.frames_per_sec_denominator == b.frames_per_sec_denominator);
}

bool UvcFormat::operator==(const fuchsia::camera::VideoFormat& vf) const {
  fuchsia::camera::VideoFormat uvf = ToFidl();

  if (!FrameRatesEqual(vf.rate, uvf.rate)) {
    return false;
  }

  for (uint32_t i = 0; i < uvf.format.planes.size(); i++) {
    if (vf.format.planes[i].byte_offset != uvf.format.planes[i].byte_offset ||
        vf.format.planes[i].bytes_per_row != uvf.format.planes[i].bytes_per_row) {
      return false;
    }
  }

  if (!fidl::Equals(vf.format.pixel_format, uvf.format.pixel_format)) {
    return false;
  }
  if (vf.format.width != uvf.format.width || vf.format.height != uvf.format.height) {
    return false;
  }
  return true;
}

}  // namespace camera::usb_video
