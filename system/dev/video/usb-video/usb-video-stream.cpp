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
    if (!usb || !intf || !input_header || !formats || !settings) {
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

    return UsbVideoStreamBase::DdkAdd(devname);
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