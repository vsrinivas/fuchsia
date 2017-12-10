// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <zircon/assert.h>
#include <fbl/type_support.h>
#include <fbl/unique_ptr.h>

// DDK block protocol support
//
// :: Mixins ::
//
// ddk::BlockIfc and ddk::BlockProtocol are mixin classes that simplify writing DDK drivers that
// interact with the block protocol. They take care of implementing the function pointer tables
// and calling into the object that wraps them.
//
// :: Examples ::
//
// // A driver that implements a ZX_PROTOCOL_BLOCK_CORE device
// class BlockDevice;
// using BlockDeviceType = ddk::Device<BlockDevice, /* ddk mixins */>;
//
// class BlockDevice : public BlockDeviceType,
//                      public ddk::BlockProtocol<BlockDevice> {
//   public:
//     BlockDevice(zx_device_t* parent)
//       : BlockDeviceType("my-block-device", parent) {}
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
class BlockProtocol : public internal::base_protocol {
  public:
    BlockProtocol() {
        // Can only inherit from one base_protocol implemenation
        ZX_ASSERT(ddk_proto_ops_ == nullptr);
        ddk_proto_id_ = ZX_PROTOCOL_BLOCK_CORE;
    }
};

}  // namespace ddk
