// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/camera/drivers/usb_video/usb-video.h"

#include <stdlib.h>
#include <zircon/hw/usb/video.h>

#include <ddk/debug.h>
#include <ddk/device.h>
#include <ddk/driver.h>
#include <ddk/platform-defs.h>
#include <ddk/protocol/usb.h>
#include <fbl/vector.h>
#include <usb/usb.h>

#include "src/camera/drivers/usb_video/usb-video-stream.h"
#include "src/camera/drivers/usb_video/usb_video-bind.h"
#include "src/camera/drivers/usb_video/uvc_format.h"

namespace {
// The biggest size we are allowing USB descriptor strings to be.
// Technically, the bLength field for strings is one byte, so the
// max size for any string should be 255.
static constexpr size_t kUsbDescriptorStringSize = 512;

// Extract strings from the device description.
// Strings in the usb device description are represented as indices
// into the 'strings' section at the end.  This function unpacks a specific
// string, given its index.
std::string FetchString(const usb_protocol_t& usb_proto, uint8_t description_index) {
  uint8_t str_buf[kUsbDescriptorStringSize];
  size_t buflen = sizeof(str_buf);
  uint16_t language_id = 0;
  zx_status_t res = usb_get_string_descriptor(&usb_proto, description_index, language_id,
                                              &language_id, str_buf, buflen, &buflen);
  if (res != ZX_OK) {
    return std::string();
  }

  buflen = std::min(buflen, sizeof(str_buf));
  return std::string(reinterpret_cast<char*>(str_buf), buflen);
}

// Extract vendor and product information from the USB device description.
video::usb::UsbDeviceInfo GetDeviceInfo(const usb_protocol_t& usb_proto) {
  usb_device_descriptor_t usb_dev_desc;
  video::usb::UsbDeviceInfo device_info;
  // Fetch our top level device descriptor, so we know stuff like the values
  // of our VID/PID.
  usb_get_device_descriptor(&usb_proto, &usb_dev_desc);
  device_info.vendor_id = usb_dev_desc.idVendor;
  device_info.product_id = usb_dev_desc.idProduct;
  // Attempt to fetch the string descriptors for our manufacturer name,
  // product name, and serial number.
  if (usb_dev_desc.iManufacturer) {
    device_info.manufacturer = FetchString(usb_proto, usb_dev_desc.iManufacturer);
  }

  if (usb_dev_desc.iProduct) {
    device_info.product_name = FetchString(usb_proto, usb_dev_desc.iProduct);
  }

  if (usb_dev_desc.iSerialNumber) {
    device_info.serial_number = FetchString(usb_proto, usb_dev_desc.iSerialNumber);
  }
  return device_info;
}

zx_status_t usb_video_parse_descriptors(void* ctx, zx_device_t* device) {
  usb_protocol_t usb;
  zx_status_t status = device_get_protocol(device, ZX_PROTOCOL_USB, &usb);
  if (status != ZX_OK) {
    return status;
  }

  size_t parent_req_size = usb_get_request_size(&usb);
  ZX_DEBUG_ASSERT(parent_req_size != 0);

  // Grab the device information, so we can use it when creating the
  // UsbVideoStream.
  auto device_info = GetDeviceInfo(usb);

  usb_desc_iter_t iter;
  status = usb_desc_iter_init(&usb, &iter);
  if (status != ZX_OK) {
    return status;
  }

  int video_source_index = 0;
  video::usb::UvcFormatList formats;
  fbl::Vector<video::usb::UsbVideoStreamingSetting> streaming_settings;

  usb_descriptor_header_t* header;
  // Most recent USB interface descriptor.
  usb_interface_descriptor_t* intf = NULL;
  // Most recent video control header.
  usb_video_vc_header_desc* control_header = NULL;
  // Most recent video streaming input header.
  usb_video_vs_input_header_desc* input_header = NULL;

  while ((header = usb_desc_iter_peek(&iter)) != NULL) {
    switch (header->bDescriptorType) {
      case USB_DT_INTERFACE_ASSOCIATION: {
        usb_interface_assoc_descriptor_t* assoc_desc =
            reinterpret_cast<usb_interface_assoc_descriptor_t*>(
                usb_desc_iter_get_structure(&iter, sizeof(usb_interface_assoc_descriptor_t)));
        if (assoc_desc == NULL) {
          break;
        }
        zxlogf(DEBUG,
               "USB_DT_INTERFACE_ASSOCIATION bInterfaceCount: %u "
               "bFirstInterface: %u\n",
               assoc_desc->bInterfaceCount, assoc_desc->bFirstInterface);
        break;
      }
      case USB_DT_INTERFACE: {
        intf = reinterpret_cast<usb_interface_descriptor_t*>(
            usb_desc_iter_get_structure(&iter, sizeof(usb_interface_descriptor_t)));
        if (intf == NULL) {
          break;
        }
        if (intf->bInterfaceClass == USB_CLASS_VIDEO) {
          if (intf->bInterfaceSubClass == USB_SUBCLASS_VIDEO_CONTROL) {
            zxlogf(DEBUG, "interface USB_SUBCLASS_VIDEO_CONTROL");
            break;
          } else if (intf->bInterfaceSubClass == USB_SUBCLASS_VIDEO_STREAMING) {
            zxlogf(DEBUG,
                   "interface USB_SUBCLASS_VIDEO_STREAMING bAlternateSetting: "
                   "%d\n",
                   intf->bAlternateSetting);
            // Encountered a new video streaming interface.
            if (intf->bAlternateSetting == 0) {
              // Create a video source if we've successfully parsed a VS
              // interface.
              if (formats.Size() > 0) {
                status = video::usb::UsbVideoStream::Create(
                    device, &usb, video_source_index++, intf, control_header, input_header,
                    std::move(formats), &streaming_settings, std::move(device_info),
                    parent_req_size);
                if (status != ZX_OK) {
                  zxlogf(ERROR, "UsbVideoStream::Create failed: %d", status);
                  goto error_return;
                }
              }
              // formats.reset();  //TODO(garratt): what to do here?
              streaming_settings.reset();
              input_header = NULL;
            }
            break;
          } else if (intf->bInterfaceSubClass == USB_SUBCLASS_VIDEO_INTERFACE_COLLECTION) {
            zxlogf(DEBUG,
                   "interface USB_SUBCLASS_VIDEO_INTERFACE_COLLECTION "
                   "bAlternateSetting: %d\n",
                   intf->bAlternateSetting);
            break;
          }
        }
        zxlogf(DEBUG, "USB_DT_INTERFACE %d %d %d", intf->bInterfaceClass, intf->bInterfaceSubClass,
               intf->bInterfaceProtocol);
        break;
      }
      case USB_VIDEO_CS_INTERFACE: {
        usb_video_vc_desc_header* vc_header = reinterpret_cast<usb_video_vc_desc_header*>(
            usb_desc_iter_get_structure(&iter, sizeof(usb_video_vc_desc_header)));
        if (vc_header == NULL) {
          break;
        }
        if (intf && intf->bInterfaceSubClass == USB_SUBCLASS_VIDEO_CONTROL) {
          switch (vc_header->bDescriptorSubtype) {
            case USB_VIDEO_VC_HEADER: {
              control_header = reinterpret_cast<usb_video_vc_header_desc*>(
                  usb_desc_iter_get_structure(&iter, sizeof(usb_video_vc_header_desc)));
              if (control_header == NULL) {
                break;
              }
              zxlogf(DEBUG, "USB_VIDEO_VC_HEADER dwClockFrequency: %u",
                     control_header->dwClockFrequency);
              break;
            }
            case USB_VIDEO_VC_INPUT_TERMINAL: {
              usb_video_vc_input_terminal_desc* desc =
                  reinterpret_cast<usb_video_vc_input_terminal_desc*>(
                      usb_desc_iter_get_structure(&iter, sizeof(usb_video_vc_input_terminal_desc)));
              if (desc == NULL) {
                break;
              }
              zxlogf(DEBUG, "USB_VIDEO_VC_INPUT_TERMINAL wTerminalType: %04X",
                     le16toh(desc->wTerminalType));
              break;
            }
            case USB_VIDEO_VC_OUTPUT_TERMINAL: {
              usb_video_vc_output_terminal_desc* desc =
                  reinterpret_cast<usb_video_vc_output_terminal_desc*>(usb_desc_iter_get_structure(
                      &iter, sizeof(usb_video_vc_output_terminal_desc)));
              if (desc == NULL) {
                break;
              }
              zxlogf(DEBUG, "USB_VIDEO_VC_OUTPUT_TERMINAL wTerminalType: %04X",
                     le16toh(desc->wTerminalType));
              break;
            }
            case USB_VIDEO_VC_SELECTOR_UNIT:
              zxlogf(DEBUG, "USB_VIDEO_VC_SELECTOR_UNIT");
              break;
            case USB_VIDEO_VC_PROCESSING_UNIT:
              zxlogf(DEBUG, "USB_VIDEO_VC_PROCESSING_UNIT");
              break;
            case USB_VIDEO_VC_EXTENSION_UNIT:
              zxlogf(DEBUG, "USB_VIDEO_VS_EXTENSION_TYPE");
              break;
            case USB_VIDEO_VC_ENCODING_UNIT:
              zxlogf(DEBUG, "USB_VIDEO_VS_ENCODING_TYPE");
              break;
          }
        } else if (intf->bInterfaceSubClass == USB_SUBCLASS_VIDEO_STREAMING) {
          switch (vc_header->bDescriptorSubtype) {
            case USB_VIDEO_VS_INPUT_HEADER: {
              input_header = reinterpret_cast<usb_video_vs_input_header_desc*>(
                  usb_desc_iter_get_structure(&iter, sizeof(usb_video_vs_input_header_desc)));
              if (input_header == NULL) {
                break;
              }
              zxlogf(DEBUG,
                     "USB_VIDEO_VS_INPUT_HEADER bNumFormats: %u "
                     "bEndpointAddress 0x%x\n",
                     input_header->bNumFormats, input_header->bEndpointAddress);
              break;
            }
            case USB_VIDEO_VS_OUTPUT_HEADER:
              zxlogf(DEBUG, "USB_VIDEO_VS_OUTPUT_HEADER");
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
              if (input_header && formats.number_of_formats() >= input_header->bNumFormats) {
                // More formats than expected, this should never happen.
                zxlogf(ERROR, "skipping unexpected format %u, already have %d formats",
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
        } else if (intf->bInterfaceSubClass == USB_SUBCLASS_VIDEO_INTERFACE_COLLECTION) {
          zxlogf(DEBUG, "USB_SUBCLASS_VIDEO_INTERFACE_COLLECTION");
        }
        break;
      }
      case USB_DT_ENDPOINT: {
        usb_endpoint_descriptor_t* endp = reinterpret_cast<usb_endpoint_descriptor_t*>(
            usb_desc_iter_get_structure(&iter, sizeof(usb_endpoint_descriptor_t)));
        if (endp == NULL) {
          break;
        }
        const char* direction =
            (endp->bEndpointAddress & USB_ENDPOINT_DIR_MASK) == USB_ENDPOINT_IN ? "IN" : "OUT";
        uint16_t max_packet_size = usb_ep_max_packet(endp);
        // The additional transactions per microframe value is extracted
        // from bits 12..11 of wMaxPacketSize, so it fits in a uint8_t.
        uint8_t per_mf = static_cast<uint8_t>(usb_ep_add_mf_transactions(endp) + 1);
        zxlogf(DEBUG,
               "USB_DT_ENDPOINT %s bEndpointAddress 0x%x packet size %d, %d / "
               "mf\n",
               direction, endp->bEndpointAddress, max_packet_size, per_mf);

        // There may be another still image endpoint, so check the address
        // matches the input header.
        if (input_header && endp->bEndpointAddress == input_header->bEndpointAddress) {
          video::usb::UsbVideoStreamingSetting setting = {.alt_setting = intf->bAlternateSetting,
                                                          .transactions_per_microframe = per_mf,
                                                          .max_packet_size = max_packet_size,
                                                          .ep_type = usb_ep_type(endp)};
          streaming_settings.push_back(setting);
        }
        break;
      }
      case USB_VIDEO_CS_ENDPOINT: {
        usb_video_vc_interrupt_endpoint_desc* desc =
            reinterpret_cast<usb_video_vc_interrupt_endpoint_desc*>(
                usb_desc_iter_get_structure(&iter, sizeof(usb_video_vc_interrupt_endpoint_desc)));
        if (desc == NULL) {
          break;
        }
        zxlogf(DEBUG, "USB_VIDEO_CS_ENDPOINT wMaxTransferSize %u", desc->wMaxTransferSize);
        break;
      }
      case USB_DT_SS_EP_COMPANION: {
        if (streaming_settings.is_empty()) {
          status = ZX_ERR_BAD_STATE;
          goto error_return;
        }
        auto& settings = streaming_settings[streaming_settings.size() - 1];

        if (settings.ep_type == USB_ENDPOINT_ISOCHRONOUS) {
          usb_ss_ep_comp_descriptor_t* desc = reinterpret_cast<usb_ss_ep_comp_descriptor_t*>(
              usb_desc_iter_get_structure(&iter, sizeof(usb_ss_ep_comp_descriptor_t)));
          if (desc == NULL) {
            break;
          }
          if (usb_ss_ep_comp_isoc_comp(desc)) {
            if (!usb_desc_iter_advance(&iter)) {
              status = ZX_ERR_BAD_STATE;
              goto error_return;
            }
            auto next = usb_desc_iter_peek(&iter);
            if (next == nullptr || next->bDescriptorType != USB_DT_SS_ISOCH_EP_COMPANION) {
              status = ZX_ERR_BAD_STATE;
              goto error_return;
            }
            usb_ss_isoch_ep_comp_descriptor_t* next_desc =
                reinterpret_cast<usb_ss_isoch_ep_comp_descriptor_t*>(
                    usb_desc_iter_get_structure(&iter, sizeof(usb_ss_isoch_ep_comp_descriptor_t)));
            if (next_desc == NULL) {
              status = ZX_ERR_BAD_STATE;
              goto error_return;
            }
            uint32_t denom = (desc->bMaxBurst + 1) * settings.max_packet_size;
            settings.transactions_per_microframe =
                (uint8_t)((next_desc->dwBytesPerInterval + denom - 1) / denom);
          } else {
            settings.transactions_per_microframe =
                (uint8_t)((desc->bMaxBurst + 1) * (usb_ss_ep_comp_isoc_mult(desc) + 1));
          }
          break;
        }
        // fall through for unhandled superspeed bulk endpoint
      }
      default:
        zxlogf(DEBUG, "unknown DT %d", header->bDescriptorType);
        break;
    }
    usb_desc_iter_advance(&iter);
  }
  if (formats.Size() > 0) {
    status = video::usb::UsbVideoStream::Create(
        device, &usb, video_source_index++, intf, control_header, input_header, std::move(formats),
        &streaming_settings, std::move(device_info), parent_req_size);
    if (status != ZX_OK) {
      zxlogf(ERROR, "UsbVideoStream::Create failed: %d", status);
    }
  }
error_return:
  usb_desc_iter_release(&iter);
  return status;
}

static zx_driver_ops_t driver_ops = []() {
  zx_driver_ops_t ops = {};
  ops.version = DRIVER_OPS_VERSION;
  ops.bind = usb_video_parse_descriptors;
  return ops;
}();

}  // namespace

// clang-format off
ZIRCON_DRIVER(usb_video, driver_ops, "zircon", "0.1");

// clang-format on
