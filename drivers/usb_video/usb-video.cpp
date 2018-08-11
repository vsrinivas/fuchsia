// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <ddk/binding.h>
#include <ddk/debug.h>
#include <ddk/device.h>
#include <ddk/driver.h>
#include <ddk/protocol/usb.h>
#include <ddk/usb/usb.h>
#include <fbl/vector.h>
#include <stdlib.h>
#include <zircon/device/usb.h>
#include <zircon/hw/usb-video.h>

#include "garnet/drivers/usb_video/usb-video-stream.h"
#include "garnet/drivers/usb_video/usb-video.h"
#include "garnet/drivers/usb_video/uvc_format.h"

namespace {

zx_status_t usb_video_parse_descriptors(void* ctx, zx_device_t* device,
                                        void** cookie) {
  usb_protocol_t usb;
  zx_status_t status = device_get_protocol(device, ZX_PROTOCOL_USB, &usb);
  if (status != ZX_OK) {
    return status;
  }

  usb_desc_iter_t iter;
  status = usb_desc_iter_init(&usb, &iter);
  if (status != ZX_OK) {
    return status;
  }

  int video_source_index = 0;
  video::usb::UvcFormatList formats;
  fbl::Vector<video::usb::UsbVideoStreamingSetting> streaming_settings;
  fbl::AllocChecker ac;

  usb_descriptor_header_t* header;
  // Most recent USB interface descriptor.
  usb_interface_descriptor_t* intf = NULL;
  // Most recent video control header.
  usb_video_vc_header_desc* control_header = NULL;
  // Most recent video streaming input header.
  usb_video_vs_input_header_desc* input_header = NULL;

  while ((header = usb_desc_iter_next(&iter)) != NULL) {
    switch (header->bDescriptorType) {
      case USB_DT_INTERFACE_ASSOCIATION: {
        usb_interface_assoc_descriptor_t* assoc_desc =
            (usb_interface_assoc_descriptor_t*)header;
        zxlogf(TRACE,
               "USB_DT_INTERFACE_ASSOCIATION bInterfaceCount: %u "
               "bFirstInterface: %u\n",
               assoc_desc->bInterfaceCount, assoc_desc->bFirstInterface);
        break;
      }
      case USB_DT_INTERFACE: {
        intf = (usb_interface_descriptor_t*)header;

        if (intf->bInterfaceClass == USB_CLASS_VIDEO) {
          if (intf->bInterfaceSubClass == USB_SUBCLASS_VIDEO_CONTROL) {
            zxlogf(TRACE, "interface USB_SUBCLASS_VIDEO_CONTROL\n");
            break;
          } else if (intf->bInterfaceSubClass == USB_SUBCLASS_VIDEO_STREAMING) {
            zxlogf(TRACE,
                   "interface USB_SUBCLASS_VIDEO_STREAMING bAlternateSetting: "
                   "%d\n",
                   intf->bAlternateSetting);
            // Encountered a new video streaming interface.
            if (intf->bAlternateSetting == 0) {
              // Create a video source if we've successfully parsed a VS
              // interface.
              if (formats.Size() > 0) {
                status = video::usb::UsbVideoStream::Create(
                    device, &usb, video_source_index++, intf, control_header,
                    input_header, std::move(formats), &streaming_settings);
                if (status != ZX_OK) {
                  zxlogf(ERROR, "UsbVideoStream::Create failed: %d\n", status);
                  goto error_return;
                }
              }
              // formats.reset();  //TODO(garratt): what to do here?
              streaming_settings.reset();
              input_header = NULL;
            }
            break;
          } else if (intf->bInterfaceSubClass ==
                     USB_SUBCLASS_VIDEO_INTERFACE_COLLECTION) {
            zxlogf(TRACE,
                   "interface USB_SUBCLASS_VIDEO_INTERFACE_COLLECTION "
                   "bAlternateSetting: %d\n",
                   intf->bAlternateSetting);
            break;
          }
        }
        zxlogf(TRACE, "USB_DT_INTERFACE %d %d %d\n", intf->bInterfaceClass,
               intf->bInterfaceSubClass, intf->bInterfaceProtocol);
        break;
      }
      case USB_VIDEO_CS_INTERFACE: {
        usb_video_vc_desc_header* vc_header = (usb_video_vc_desc_header*)header;
        if (intf->bInterfaceSubClass == USB_SUBCLASS_VIDEO_CONTROL) {
          switch (vc_header->bDescriptorSubtype) {
            case USB_VIDEO_VC_HEADER: {
              control_header = (usb_video_vc_header_desc*)header;
              zxlogf(TRACE, "USB_VIDEO_VC_HEADER dwClockFrequency: %u\n",
                     control_header->dwClockFrequency);
              break;
            }
            case USB_VIDEO_VC_INPUT_TERMINAL: {
              usb_video_vc_input_terminal_desc* desc =
                  (usb_video_vc_input_terminal_desc*)header;
              zxlogf(TRACE, "USB_VIDEO_VC_INPUT_TERMINAL wTerminalType: %04X\n",
                     le16toh(desc->wTerminalType));
              break;
            }
            case USB_VIDEO_VC_OUTPUT_TERMINAL: {
              usb_video_vc_output_terminal_desc* desc =
                  (usb_video_vc_output_terminal_desc*)header;
              zxlogf(TRACE,
                     "USB_VIDEO_VC_OUTPUT_TERMINAL wTerminalType: %04X\n",
                     le16toh(desc->wTerminalType));
              break;
            }
            case USB_VIDEO_VC_SELECTOR_UNIT:
              zxlogf(TRACE, "USB_VIDEO_VC_SELECTOR_UNIT\n");
              break;
            case USB_VIDEO_VC_PROCESSING_UNIT:
              zxlogf(TRACE, "USB_VIDEO_VC_PROCESSING_UNIT\n");
              break;
            case USB_VIDEO_VC_EXTENSION_UNIT:
              zxlogf(TRACE, "USB_VIDEO_VS_EXTENSION_TYPE\n");
              break;
            case USB_VIDEO_VC_ENCODING_UNIT:
              zxlogf(TRACE, "USB_VIDEO_VS_ENCODING_TYPE\n");
              break;
          }
        } else if (intf->bInterfaceSubClass == USB_SUBCLASS_VIDEO_STREAMING) {
          switch (vc_header->bDescriptorSubtype) {
            case USB_VIDEO_VS_INPUT_HEADER: {
              input_header = (usb_video_vs_input_header_desc*)header;
              zxlogf(TRACE,
                     "USB_VIDEO_VS_INPUT_HEADER bNumFormats: %u "
                     "bEndpointAddress 0x%x\n",
                     input_header->bNumFormats, input_header->bEndpointAddress);
              break;
            }
            case USB_VIDEO_VS_OUTPUT_HEADER:
              zxlogf(TRACE, "USB_VIDEO_VS_OUTPUT_HEADER\n");
              break;
            case USB_VIDEO_VS_FORMAT_UNCOMPRESSED:
            case USB_VIDEO_VS_FORMAT_MJPEG:
            case USB_VIDEO_VS_FORMAT_MPEG2TS:
            case USB_VIDEO_VS_FORMAT_DV:
            case USB_VIDEO_VS_FORMAT_FRAME_BASED:
            case USB_VIDEO_VS_FORMAT_STREAM_BASED:
            case USB_VIDEO_VS_FORMAT_H264:
            case USB_VIDEO_VS_FORMAT_H264_SIMULCAST:
            case USB_VIDEO_VS_FORMAT_VP8:
            case USB_VIDEO_VS_FORMAT_VP8_SIMULCAST:
              if (formats.number_of_formats() >= input_header->bNumFormats) {
                // More formats than expected, this should never happen.
                zxlogf(
                    ERROR,
                    "skipping unexpected format %u, already have %d formats\n",
                    vc_header->bDescriptorSubtype, input_header->bNumFormats);
                break;
              }
              status = formats.ParseUsbDescriptor(vc_header, &iter);
              if (status == ZX_ERR_NO_MEMORY) {
                goto error_return;
              }
              // ParseUsbDescriptor will return an error if it's an unsupported
              // format, but we shouldn't return early in case the device has
              // other formats we support.
              break;
          }
        } else if (intf->bInterfaceSubClass ==
                   USB_SUBCLASS_VIDEO_INTERFACE_COLLECTION) {
          zxlogf(TRACE, "USB_SUBCLASS_VIDEO_INTERFACE_COLLECTION\n");
        }
        break;
      }
      case USB_DT_ENDPOINT: {
        usb_endpoint_descriptor_t* endp = (usb_endpoint_descriptor_t*)header;
        const char* direction =
            (endp->bEndpointAddress & USB_ENDPOINT_DIR_MASK) == USB_ENDPOINT_IN
                ? "IN"
                : "OUT";
        uint16_t max_packet_size = usb_ep_max_packet(endp);
        // The additional transactions per microframe value is extracted
        // from bits 12..11 of wMaxPacketSize, so it fits in a uint8_t.
        uint8_t per_mf =
            static_cast<uint8_t>(usb_ep_add_mf_transactions(endp) + 1);
        zxlogf(TRACE,
               "USB_DT_ENDPOINT %s bEndpointAddress 0x%x packet size %d, %d / "
               "mf\n",
               direction, endp->bEndpointAddress, max_packet_size, per_mf);

        // There may be another still image endpoint, so check the address
        // matches the input header.
        if (input_header &&
            endp->bEndpointAddress == input_header->bEndpointAddress) {
          video::usb::UsbVideoStreamingSetting setting = {
              .alt_setting = intf->bAlternateSetting,
              .transactions_per_microframe = per_mf,
              .max_packet_size = max_packet_size,
              .ep_type = usb_ep_type(endp)};
          streaming_settings.push_back(setting, &ac);
          if (!ac.check()) {
            status = ZX_ERR_NO_MEMORY;
            goto error_return;
          }
        }
        break;
      }
      case USB_VIDEO_CS_ENDPOINT: {
        usb_video_vc_interrupt_endpoint_desc* desc =
            (usb_video_vc_interrupt_endpoint_desc*)header;
        zxlogf(TRACE, "USB_VIDEO_CS_ENDPOINT wMaxTransferSize %u\n",
               desc->wMaxTransferSize);
        break;
      }
      default:
        zxlogf(TRACE, "unknown DT %d\n", header->bDescriptorType);
        break;
    }
  }
  if (formats.Size() > 0) {
    status = video::usb::UsbVideoStream::Create(
        device, &usb, video_source_index++, intf, control_header, input_header,
        std::move(formats), &streaming_settings);
    if (status != ZX_OK) {
      zxlogf(ERROR, "UsbVideoStream::Create failed: %d\n", status);
    }
  }
error_return:
  usb_desc_iter_release(&iter);
  return status;
}

}  // namespace

extern "C" zx_status_t usb_video_bind(void* ctx, zx_device_t* device,
                                      void** cookie) {
  return usb_video_parse_descriptors(ctx, device, cookie);
}
