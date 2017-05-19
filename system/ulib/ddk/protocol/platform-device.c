// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <ddk/device.h>
#include <ddk/protocol/platform-device.h>

mx_status_t platform_device_find_protocol(mx_device_t* pdev, uint32_t proto_id,
                                          mx_device_t** out_dev, void** out_proto) {
    platform_device_protocol_t* proto;
    mx_status_t status = device_op_get_protocol(pdev, MX_PROTOCOL_PLATFORM_DEV, (void**)&proto);
    if (status !=  NO_ERROR) {
        return status;
    }

    return proto->find_protocol(pdev, proto_id, out_dev, out_proto);
}
