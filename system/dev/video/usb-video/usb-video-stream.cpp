// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <ddk/debug.h>
#include <fbl/ref_ptr.h>
#include <fbl/unique_ptr.h>
#include <fbl/vector.h>
#include <stdlib.h>
#include <string.h>

#include "usb-video-stream.h"
#include "video-util.h"

namespace video {
namespace usb {

// static
zx_status_t UsbVideoStream::Create(zx_device_t* device,
                                   usb_protocol_t* usb,
                                   int index,
                                   usb_interface_descriptor_t* intf,
                                   usb_video_vs_input_header_desc* input_header,
                                   fbl::Vector<UsbVideoFormat>* formats,
                                   fbl::Vector<UsbVideoStreamingSetting>* settings) {
    if (!usb || !intf || !input_header || !formats || formats->size() == 0 || !settings) {
        return ZX_ERR_INVALID_ARGS;
    }
    auto dev = fbl::unique_ptr<UsbVideoStream>(
        new UsbVideoStream(device, usb, formats, settings));

    char name[ZX_DEVICE_NAME_MAX];
    snprintf(name, sizeof(name), "usb-video-source-%d", index);

    auto status = dev->Bind(name, intf, input_header);
    if (status == ZX_OK) {
        // devmgr is now in charge of the memory for dev
        dev.release();
    }
    return status;
}

zx_status_t UsbVideoStream::Bind(const char* devname,
                                 usb_interface_descriptor_t* intf,
                                 usb_video_vs_input_header_desc* input_header) {
    iface_num_ = intf->bInterfaceNumber;
    usb_ep_addr_ = input_header->bEndpointAddress;

    // TODO(jocelyndang): add a way for the client to select the format and
    // frame type. Just use the first format for now.
    UsbVideoFormat* format = formats_.get();
    if (!format) {
        return ZX_ERR_NOT_SUPPORTED;
    }
    const UsbVideoFrameDesc* try_frame = NULL;
    // Try the recommended frame descriptor, if any.
    if (format->default_frame_index != 0) {
        for (const auto& frame : format->frame_descs) {
            if (frame.index == format->default_frame_index) {
                try_frame = &frame;
            }
        }
        if (!try_frame) {
            return ZX_ERR_INTERNAL;
        }
    }
    zx_status_t status = TryFormat(format, try_frame, &negotiation_result_,
                                   &cur_streaming_setting_);
    if (status != ZX_OK) {
        // Negotiation failed. Try a different frame descriptor.
        for (const auto& frame : format->frame_descs) {
            if (frame.index == format->default_frame_index) {
                // Already tried this setting.
                continue;
            }
            try_frame = &frame;
            status = TryFormat(format, try_frame, &negotiation_result_,
                               &cur_streaming_setting_);
            if (status == ZX_OK) {
                break;
            }
        }
    }
    if (status != ZX_OK) {
        zxlogf(ERROR, "failed to set format %u: error %d\n", format->index, status);
        return status;
    }

    cur_format_ = format;
    cur_frame_desc_ = try_frame;

    zxlogf(INFO, "configured video: format index %u frame index %u\n",
           cur_format_->index, cur_frame_desc_->index);
    zxlogf(INFO, "alternate setting %d, packet size %u transactions per mf %u\n",
           cur_streaming_setting_->alt_setting,
           cur_streaming_setting_->max_packet_size,
           cur_streaming_setting_->transactions_per_microframe);
    return UsbVideoStreamBase::DdkAdd(devname);
}

zx_status_t UsbVideoStream::TryFormat(const UsbVideoFormat* format,
                                      const UsbVideoFrameDesc* frame_desc,
                                      usb_video_vc_probe_and_commit_controls* out_result,
                                      const UsbVideoStreamingSetting** out_setting) {
    zxlogf(INFO, "trying format %u, frame desc %u\n",
           format->index, frame_desc->index);

    usb_video_vc_probe_and_commit_controls proposal;
    memset(&proposal, 0, sizeof(usb_video_vc_probe_and_commit_controls));
    proposal.bmHint = USB_VIDEO_BM_HINT_FRAME_INTERVAL;
    proposal.bFormatIndex = format->index;

    // Some formats do not have frame descriptors.
    if (frame_desc != NULL) {
        proposal.bFrameIndex = frame_desc->index;
        proposal.dwFrameInterval = frame_desc->default_frame_interval;
    }

    zx_status_t status = usb_video_negotiate_stream(
        &usb_, iface_num_, &proposal, out_result);
    if (status != ZX_OK) {
        zxlogf(ERROR, "usb_video_negotiate_stream failed: %d\n", status);
        return status;
    }

    // TODO(jocelyndang): we should calculate this ourselves instead
    // of reading the reported value, as it is incorrect in some devices.
    uint32_t required_bandwidth = negotiation_result_.dwMaxPayloadTransferSize;

    bool found = false;
    // Find a setting that supports the required bandwidth.
    for (const auto& setting : streaming_settings_) {
        uint32_t bandwidth =
            setting.max_packet_size * setting.transactions_per_microframe;
        // TODO(jocelyndang): figure out why multiple transactions per microframe
        // isn't working for usb video.
        if (setting.transactions_per_microframe == 1 &&
            bandwidth >= required_bandwidth) {
            found = true;
            *out_setting = &setting;
            break;
        }
    }
    if (!found) {
        zxlogf(ERROR, "could not find a setting with bandwidth >= %u\n",
               required_bandwidth);
        return ZX_ERR_NOT_SUPPORTED;
    }
    return ZX_OK;
}

void UsbVideoStream::DdkUnbind() {
    // Unpublish our device node.
    DdkRemove();
}

void UsbVideoStream::DdkRelease() {
    delete this;
}

} // namespace usb
} // namespace video