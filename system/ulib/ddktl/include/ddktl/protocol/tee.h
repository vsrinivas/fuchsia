// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <fbl/type_support.h>
#include <fbl/unique_ptr.h>
#include <zircon/assert.h>

// DDK tee protocol support
//
// :: Mixins ::
//
// ddk::TeeProtocol is a mixin class that simplifies writing DDK drivers that
// interact with the TEE protocol. It takes care of implementing the function pointer tables and
// calling into the object that wraps it.
//
// :: Examples ::
//
// // A driver that implements a ZX_PROTOCOL_TEE device
// class TeeDevice;
// using TeeDeviceType = ddk::Device<TeeDevice, /* ddk mixins */>;
//
// class TeeDevice : public TeeDeviceType,
//                   public ddk::TeeProtocol<TeeDevice> {
//   public:
//     TeeDevice(zx_device_t* parent)
//       : TeeDeviceType("my-tee-device", parent) {}
//
//     zx_status_t Bind() {
//         DdkAdd();
//     }
//
//     void DdkRelease() {
//         // Clean up
//     }
//
//     ...
//   private:
//     ...
// };

namespace ddk {

template <typename D>
class TeeProtocol : public internal::base_protocol {
public:
    TeeProtocol() {
        // Can only inherit from one base_protocol implemenation
        ZX_ASSERT(ddk_proto_id_ == 0);
        ddk_proto_id_ = ZX_PROTOCOL_TEE;
        ddk_proto_ops_ = &ops_;
    }

private:
    // Empty struct to use for ops, so that we do not break the invariant that ddk_proto_ops_ is
    // non-null for devices with a protocol.
    struct {
    } ops_;
};

} // namespace ddk
