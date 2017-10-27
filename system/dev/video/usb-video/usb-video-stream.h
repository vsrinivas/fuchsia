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
 //         usb_(*usb),
          formats_(fbl::move(*formats)),
          streaming_settings_(fbl::move(*settings)) {}

    zx_status_t Bind(const char* devname,
                     usb_interface_descriptor_t* intf,
                     usb_video_vs_input_header_desc* input_header);

//    usb_protocol_t usb_;

    fbl::Vector<UsbVideoFormat> formats_;
    fbl::Vector<UsbVideoStreamingSetting> streaming_settings_;

    uint8_t iface_num_   = 0;
    uint8_t usb_ep_addr_ = 0;
};

} // namespace usb
} // namespace video
