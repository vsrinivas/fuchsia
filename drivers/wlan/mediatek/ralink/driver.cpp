// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <ddk/device.h>
#include <ddk/driver.h>
#include <driver/usb.h>

#include <cstdio>
#include <cstdint>
#include <future>
#include <thread>
#include <vector>

#include "device.h"
#include "logging.h"

extern "C" mx_status_t ralink_bind(void* ctx, mx_device_t* device, void** cookie) {
    std::printf("%s\n", __func__);

    usb_protocol_t usb;
    mx_status_t result = device_get_protocol(device, MX_PROTOCOL_USB, &usb);
    if (result != MX_OK) {
        return result;
    }

    usb_desc_iter_t iter;
    result = usb_desc_iter_init(&usb, &iter);
    if (result < 0) return result;

    usb_interface_descriptor_t* intf = usb_desc_iter_next_interface(&iter, true);
    if (!intf || intf->bNumEndpoints < 3) {
        usb_desc_iter_release(&iter);
        return MX_ERR_NOT_SUPPORTED;
    }

    uint8_t blkin_endpt = 0;
    std::vector<uint8_t> blkout_endpts;

    auto endpt = usb_desc_iter_next_endpoint(&iter);
    while (endpt) {
        if (usb_ep_direction(endpt) == USB_ENDPOINT_OUT) {
            blkout_endpts.push_back(endpt->bEndpointAddress);
        } else if (usb_ep_type(endpt) == USB_ENDPOINT_BULK) {
            blkin_endpt = endpt->bEndpointAddress;
        }
        endpt = usb_desc_iter_next_endpoint(&iter);
    }
    usb_desc_iter_release(&iter);

    if (!blkin_endpt || blkout_endpts.empty()) {
        std::printf("%s could not find endpoints\n", __func__);
        return MX_ERR_NOT_SUPPORTED;
    }

    auto rtdev = new ralink::Device(device, &usb, blkin_endpt, std::move(blkout_endpts));
    auto f = std::async(std::launch::async, [rtdev]() {
                auto status = rtdev->Bind();
                if (status != MX_OK) {
                    delete rtdev;
                }
            });
    return MX_OK;
}
