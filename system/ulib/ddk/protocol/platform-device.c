// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <ddk/device.h>
#include <ddk/protocol/platform-device.h>

mx_status_t platform_device_find_protocol(mx_device_t* pdev, uint32_t proto_id, void* out) {
    platform_device_protocol_t proto;
    mx_status_t status = device_get_protocol(pdev, MX_PROTOCOL_PLATFORM_DEV, &proto);
    if (status !=  MX_OK) {
        return status;
    }

    return proto.ops->find_protocol(proto.ctx, proto_id, out);
}
