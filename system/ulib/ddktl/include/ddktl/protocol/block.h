// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <ddk/protocol/block.h>
#include <ddktl/protocol/block-internal.h>
#include <fbl/type_support.h>
#include <fbl/unique_ptr.h>
#include <zircon/assert.h>

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
// // A driver that implements a ZX_PROTOCOL_BLOCK_IMPL device
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
        internal::CheckBlockProtocolSubclass<D>();
        ops_.query = Query;
        ops_.queue = Queue;

        // Can only inherit from one base_protocol implemenation
        ZX_ASSERT(ddk_proto_id_ == 0);
        ddk_proto_id_ = ZX_PROTOCOL_BLOCK_IMPL;
        ddk_proto_ops_ = &ops_;
    }

  private:
    static void Query(void* ctx, block_info_t* info_out, size_t* block_op_size_out) {
        static_cast<D*>(ctx)->BlockQuery(info_out, block_op_size_out);
    }

    static void Queue(void* ctx, block_op_t* txn) {
        static_cast<D*>(ctx)->BlockQueue(txn);
    }

    block_protocol_ops_t ops_ = {};
};

}  // namespace ddk
