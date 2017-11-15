// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <ddktl/device-internal.h>
#include <ddktl/device.h>
#include <driver/usb.h>
#include <fbl/ref_counted.h>
#include <fbl/vector.h>

#include "usb-video.h"

namespace video {
namespace usb {

struct VideoStreamProtocol : public ddk::internal::base_protocol {
    explicit VideoStreamProtocol() {
        ddk_proto_id_ = ZX_PROTOCOL_CAMERA;
    }
};

class UsbVideoStream;
using UsbVideoStreamBase = ddk::Device<UsbVideoStream,
                                       ddk::Unbindable>;

class UsbVideoStream : public UsbVideoStreamBase,
                       public VideoStreamProtocol {

public:
    static zx_status_t Create(zx_device_t* device,
                              usb_protocol_t* usb,
                              int index,
                              usb_interface_descriptor_t* intf,
                              usb_video_vs_input_header_desc* input_header,
                              fbl::Vector<UsbVideoFormat>* formats,
                              fbl::Vector<UsbVideoStreamingSetting>* settings);

    // DDK device implementation
    void DdkUnbind();
    void DdkRelease();

private:
    UsbVideoStream(zx_device_t* parent,
                   usb_protocol_t* usb,
                   fbl::Vector<UsbVideoFormat>* formats,
                   fbl::Vector<UsbVideoStreamingSetting>* settings)
        : UsbVideoStreamBase(parent),
          usb_(*usb),
          formats_(fbl::move(*formats)),
          streaming_settings_(fbl::move(*settings)) {}

    zx_status_t Bind(const char* devname,
                     usb_interface_descriptor_t* intf,
                     usb_video_vs_input_header_desc* input_header);

    // Requests the device use the given format and frame descriptor,
    // then finds a streaming setting that supports the required
    // data throughput.
    // If successful, out_result will be populated with the result
    // of the stream negotiation, and out_setting will be populated
    // with a pointer to the selected streaming setting.
    // Otherwise an error will be returned and the caller should try
    // again with a different set of inputs.
    //
    // frame_desc may be NULL for non frame based formats.
    zx_status_t TryFormat(const UsbVideoFormat* format,
                          const UsbVideoFrameDesc* frame_desc,
                          usb_video_vc_probe_and_commit_controls* out_result,
                          const UsbVideoStreamingSetting** out_setting);

    usb_protocol_t usb_;

    fbl::Vector<UsbVideoFormat> formats_;
    fbl::Vector<UsbVideoStreamingSetting> streaming_settings_;

    usb_video_vc_probe_and_commit_controls negotiation_result_;
    const UsbVideoFormat* cur_format_;
    const UsbVideoFrameDesc* cur_frame_desc_;
    const UsbVideoStreamingSetting* cur_streaming_setting_;

    uint8_t iface_num_   = 0;
    uint8_t usb_ep_addr_ = 0;
};

} // namespace usb
} // namespace video
