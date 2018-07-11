// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <ddk/binding.h>
#include <ddk/debug.h>
#include <ddk/device.h>
#include <ddk/driver.h>
#include <ddk/protocol/usb.h>
#include <driver/usb.h>
#include <stdlib.h>
#include <zircon/device/usb.h>
#include <zircon/hw/usb-video.h>
#include <fbl/vector.h>

#include "usb-video.h"
#include "usb-video-stream.h"

namespace {

// 8 bits for each RGB.
static constexpr uint32_t MJPEG_BITS_PER_PIXEL = 24;

camera::camera_proto::PixelFormat guid_to_pixel_format(uint8_t guid[GUID_LENGTH]) {
    struct {
        uint8_t guid[GUID_LENGTH];
        camera::camera_proto::PixelFormat pixel_format;
    } GUID_LUT[] = {
        { USB_VIDEO_GUID_YUY2_VALUE, YUY2 },
        { USB_VIDEO_GUID_NV12_VALUE, NV12 },
        { USB_VIDEO_GUID_M420_VALUE, M420 },
        { USB_VIDEO_GUID_I420_VALUE, I420 },
    };

    for (const auto& g : GUID_LUT) {
        if (memcmp(g.guid, guid, GUID_LENGTH) == 0) {
            return g.pixel_format;
        }
    }
    return INVALID;
}

// Parses the payload format descriptor and any corresponding frame descriptors.
// The result is stored in out_format.
zx_status_t parse_format(usb_video_vc_desc_header* format_desc,
                         usb_desc_iter_t* iter,
                         video::usb::UsbVideoFormat* out_format) {
    uint8_t want_frame_type = 0;
    int want_num_frame_descs = 0;

    switch (format_desc->bDescriptorSubtype) {
    case USB_VIDEO_VS_FORMAT_UNCOMPRESSED: {
        usb_video_vs_uncompressed_format_desc* uncompressed_desc =
            (usb_video_vs_uncompressed_format_desc *)format_desc;
        zxlogf(TRACE,
               "USB_VIDEO_VS_FORMAT_UNCOMPRESSED bNumFrameDescriptors %u bBitsPerPixel %u\n",
               uncompressed_desc->bNumFrameDescriptors, uncompressed_desc->bBitsPerPixel);

        want_frame_type = USB_VIDEO_VS_FRAME_UNCOMPRESSED;
        out_format->index = uncompressed_desc->bFormatIndex;
        out_format->pixel_format =
            guid_to_pixel_format(uncompressed_desc->guidFormat);
        out_format->bits_per_pixel = uncompressed_desc->bBitsPerPixel;
        want_num_frame_descs = uncompressed_desc->bNumFrameDescriptors;
        out_format->default_frame_index = uncompressed_desc->bDefaultFrameIndex;
        break;
    }
    case USB_VIDEO_VS_FORMAT_MJPEG: {
        usb_video_vs_mjpeg_format_desc* mjpeg_desc =
            (usb_video_vs_mjpeg_format_desc *)format_desc;
        zxlogf(TRACE,
               "USB_VIDEO_VS_FORMAT_MJPEG bNumFrameDescriptors %u bmFlags %d\n",
               mjpeg_desc->bNumFrameDescriptors, mjpeg_desc->bmFlags);

        want_frame_type = USB_VIDEO_VS_FRAME_MJPEG;
        out_format->index = mjpeg_desc->bFormatIndex;
        out_format->pixel_format = MJPEG;
        out_format->bits_per_pixel = MJPEG_BITS_PER_PIXEL;
        want_num_frame_descs = mjpeg_desc->bNumFrameDescriptors;
        out_format->default_frame_index = mjpeg_desc->bDefaultFrameIndex;
        break;
    }
    // TODO(jocelyndang): handle other formats.
    default:
        zxlogf(ERROR, "unsupported format bDescriptorSubtype %u\n",
               format_desc->bDescriptorSubtype);
        return ZX_ERR_NOT_SUPPORTED;
    }

    fbl::AllocChecker ac;
    out_format->frame_descs.reserve(want_num_frame_descs, &ac);
    if (!ac.check()) {
        return ZX_ERR_NO_MEMORY;
    }
    // The format descriptor mut be immediately followed by its frame descriptors, if any.
    int num_frame_descs_found = 0;

    usb_descriptor_header_t* header;
    while ((header = usb_desc_iter_peek(iter)) != NULL &&
           header->bDescriptorType == USB_VIDEO_CS_INTERFACE &&
           num_frame_descs_found < want_num_frame_descs) {
        usb_video_vc_desc_header* format_desc = (usb_video_vc_desc_header *)header;
        if (format_desc->bDescriptorSubtype != want_frame_type) {
            break;
        }

        switch (format_desc->bDescriptorSubtype) {
        case USB_VIDEO_VS_FRAME_UNCOMPRESSED:
        case USB_VIDEO_VS_FRAME_MJPEG: {
            usb_video_vs_frame_desc* desc = (usb_video_vs_frame_desc *)header;

            // Intervals are specified in 100 ns units.
            double framesPerSec = 1 / (desc->dwDefaultFrameInterval * 100 / 1e9);
            zxlogf(TRACE, "%s (%u x %u) %.2f frames / sec\n",
                   format_desc->bDescriptorSubtype == USB_VIDEO_VS_FRAME_UNCOMPRESSED ?
                   "USB_VIDEO_VS_FRAME_UNCOMPRESSED" : "USB_VIDEO_VS_FRAME_MJPEG",
                   desc->wWidth, desc->wHeight, framesPerSec);

            video::usb::UsbVideoFrameDesc frame_desc = {
                .index = desc->bFrameIndex,
                .capture_type = STREAM,
                .default_frame_interval = desc->dwDefaultFrameInterval,
                .width = desc->wWidth,
                .height = desc->wHeight,
                .stride = desc->dwMaxVideoFrameBufferSize / desc->wHeight
            };
            out_format->frame_descs.push_back(frame_desc);
            break;
        }
        default:
            zxlogf(ERROR, "unhandled frame type: %u\n", format_desc->bDescriptorSubtype);
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

zx_status_t usb_video_parse_descriptors(void* ctx, zx_device_t* device, void** cookie) {
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
    fbl::Vector<video::usb::UsbVideoFormat> formats;
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
            zxlogf(TRACE, "USB_DT_INTERFACE_ASSOCIATION bInterfaceCount: %u bFirstInterface: %u\n",
                   assoc_desc->bInterfaceCount, assoc_desc->bFirstInterface);
            break;
        }
        case USB_DT_INTERFACE: {
            intf = (usb_interface_descriptor_t *)header;

            if (intf->bInterfaceClass == USB_CLASS_VIDEO) {
                if (intf->bInterfaceSubClass == USB_SUBCLASS_VIDEO_CONTROL) {
                    zxlogf(TRACE, "interface USB_SUBCLASS_VIDEO_CONTROL\n");
                    break;
                } else if (intf->bInterfaceSubClass == USB_SUBCLASS_VIDEO_STREAMING) {
                    zxlogf(TRACE, "interface USB_SUBCLASS_VIDEO_STREAMING bAlternateSetting: %d\n",
                           intf->bAlternateSetting);
                    // Encountered a new video streaming interface.
                    if (intf->bAlternateSetting == 0) {
                        // Create a video source if we've successfully parsed a VS interface.
                        if (formats.size() > 0) {
                            status = video::usb::UsbVideoStream::Create(
                                device, &usb, video_source_index++, intf, control_header,
                                input_header, &formats, &streaming_settings);
                            if (status != ZX_OK) {
                                zxlogf(ERROR, "UsbVideoStream::Create failed: %d\n", status);
                                goto error_return;
                            }
                        }
                        formats.reset();
                        streaming_settings.reset();
                        input_header = NULL;
                    }
                    break;
                } else if (intf->bInterfaceSubClass == USB_SUBCLASS_VIDEO_INTERFACE_COLLECTION) {
                    zxlogf(TRACE, "interface USB_SUBCLASS_VIDEO_INTERFACE_COLLECTION bAlternateSetting: %d\n",
                           intf->bAlternateSetting);
                    break;
                }
            }
            zxlogf(TRACE, "USB_DT_INTERFACE %d %d %d\n", intf->bInterfaceClass,
                   intf->bInterfaceSubClass, intf->bInterfaceProtocol);
            break;
        }
        case USB_VIDEO_CS_INTERFACE: {
            usb_video_vc_desc_header* vc_header = (usb_video_vc_desc_header *)header;
            if (intf->bInterfaceSubClass == USB_SUBCLASS_VIDEO_CONTROL) {
                switch (vc_header->bDescriptorSubtype) {
                case USB_VIDEO_VC_HEADER: {
                    control_header = (usb_video_vc_header_desc *)header;
                    zxlogf(TRACE, "USB_VIDEO_VC_HEADER dwClockFrequency: %u\n",
                           control_header->dwClockFrequency);
                    break;
                }
                case USB_VIDEO_VC_INPUT_TERMINAL: {
                    usb_video_vc_input_terminal_desc* desc =
                        (usb_video_vc_input_terminal_desc *)header;
                    zxlogf(TRACE, "USB_VIDEO_VC_INPUT_TERMINAL wTerminalType: %04X\n",
                           le16toh(desc->wTerminalType));
                    break;
                }
                case USB_VIDEO_VC_OUTPUT_TERMINAL: {
                    usb_video_vc_output_terminal_desc* desc =
                        (usb_video_vc_output_terminal_desc *)header;
                    zxlogf(TRACE, "USB_VIDEO_VC_OUTPUT_TERMINAL wTerminalType: %04X\n",
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
                    input_header = (usb_video_vs_input_header_desc *)header;
                    zxlogf(TRACE,
                           "USB_VIDEO_VS_INPUT_HEADER bNumFormats: %u bEndpointAddress 0x%x\n",
                           input_header->bNumFormats, input_header->bEndpointAddress);
                    formats.reserve(input_header->bNumFormats, &ac);
                    if (!ac.check()) {
                        status = ZX_ERR_NO_MEMORY;
                        goto error_return;
                    }
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
                    if (formats.size() >= input_header->bNumFormats) {
                        // More formats than expected, this should never happen.
                        zxlogf(ERROR, "skipping unexpected format %u, already have %d formats\n",
                               vc_header->bDescriptorSubtype, input_header->bNumFormats);
                        break;
                    }
                    video::usb::UsbVideoFormat format;
                    status = parse_format(vc_header, &iter, &format);
                    if (status == ZX_OK) {
                        formats.push_back(fbl::move(format));
                    }
                    // parse_format will return an error if it's an unsupported format, but
                    // we shouldn't return early in case the device has other formats we support.
                    break;
                }
            } else if (intf->bInterfaceSubClass == USB_SUBCLASS_VIDEO_INTERFACE_COLLECTION) {
                zxlogf(TRACE, "USB_SUBCLASS_VIDEO_INTERFACE_COLLECTION\n");
            }
            break;
        }
        case USB_DT_ENDPOINT: {
            usb_endpoint_descriptor_t* endp = (usb_endpoint_descriptor_t *)header;
            const char* direction = (endp->bEndpointAddress & USB_ENDPOINT_DIR_MASK)
                                     == USB_ENDPOINT_IN ? "IN" : "OUT";
            uint16_t max_packet_size = usb_ep_max_packet(endp);
            // The additional transactions per microframe value is extracted
            // from bits 12..11 of wMaxPacketSize, so it fits in a uint8_t.
            uint8_t per_mf = static_cast<uint8_t>(usb_ep_add_mf_transactions(endp) + 1);
            zxlogf(TRACE, "USB_DT_ENDPOINT %s bEndpointAddress 0x%x packet size %d, %d / mf\n",
                   direction, endp->bEndpointAddress, max_packet_size, per_mf);

            // There may be another still image endpoint, so check the address matches
            // the input header.
            if (input_header && endp->bEndpointAddress == input_header->bEndpointAddress) {
                video::usb::UsbVideoStreamingSetting setting = {
                    .alt_setting = intf->bAlternateSetting,
                    .transactions_per_microframe = per_mf,
                    .max_packet_size = max_packet_size,
                    .ep_type = usb_ep_type(endp)
                };
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
                (usb_video_vc_interrupt_endpoint_desc *)header;
            zxlogf(TRACE, "USB_VIDEO_CS_ENDPOINT wMaxTransferSize %u\n", desc->wMaxTransferSize);
            break;
        }
        default:
            zxlogf(TRACE, "unknown DT %d\n", header->bDescriptorType);
            break;
        }
    }
    if (formats.size() > 0) {
        status = video::usb::UsbVideoStream::Create(device, &usb, video_source_index++,
            intf, control_header, input_header, &formats, &streaming_settings);
        if (status != ZX_OK) {
            zxlogf(ERROR, "UsbVideoStream::Create failed: %d\n", status);
        }
    }
error_return:
    usb_desc_iter_release(&iter);
    return status;
}

} // namespace

extern "C" zx_status_t usb_video_bind(void* ctx, zx_device_t* device, void** cookie) {
    return usb_video_parse_descriptors(ctx, device, cookie);
}
