// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <ddktl/device-internal.h>
#include <zircon/assert.h>

// DDK skip-block protocol support.
//
// :: Mixins ::
//
// ddk::SkipBlockProtocol is a mixin class that simplifies writing DDK drivers that
// implement the skip-block protocol.
//
// :: Examples ::
//
// // A driver that implements a ZX_PROTOCOL_NAND device.
// class SkipBlockDevice;
// using SkipBlockDeviceType = ddk::Device<SkipBlockDevice, /* ddk mixins */>;
//
// class SkipBlockDevice : public SkipBlockDeviceType,
//                         public ddk::SkipBlockProtocol {
//   public:
//     SkipBlockDevice(zx_device_t* parent)
//       : SkipBlockDeviceType("my-skip-block-device", parent) {}
//     ...
// };

namespace ddk {

class SkipBlockProtocol : public internal::base_protocol {
public:
    SkipBlockProtocol() {
        // Can only inherit from one base_protocol implementation.
        ZX_ASSERT(ddk_proto_id_ == 0);
        ddk_proto_id_ = ZX_PROTOCOL_SKIP_BLOCK;
        ddk_proto_ops_ = nullptr;
    }
};

} // namespace ddk
