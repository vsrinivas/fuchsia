// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <ddk/protocol/block.h>
#include <ddktl/protocol/block-internal.h>
#include <magenta/assert.h>
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
// // A driver that implements a MX_PROTOCOL_BLOCK_CORE device
// class BlockDevice;
// using BlockDeviceType = ddk::Device<BlockDevice, /* ddk mixins */>;
//
// class BlockDevice : public BlockDeviceType,
//                      public ddk::BlockProtocol<BlockDevice> {
//   public:
//     BlockDevice(mx_device_t* parent)
//       : BlockDeviceType("my-block-device", parent) {}
//
//     mx_status_t Bind() {
//         DdkAdd();
//     }
//
//     void DdkRelease() {
//         // Clean up
//     }
//
//     void BlockSetCallbacks(block_callbacks_t* cb) {
//         // Fill out callbacks
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
        ops_.set_callbacks = SetCallbacks;
        ops_.get_info = GetInfo;
        ops_.read = Read;
        ops_.write = Write;

        // Can only inherit from one base_protocol implemenation
        MX_ASSERT(ddk_proto_ops_ == nullptr);
        ddk_proto_id_ = MX_PROTOCOL_BLOCK_CORE;
        ddk_proto_ops_ = &ops_;
    }

  private:
    static void SetCallbacks(void* ctx, block_callbacks_t* cb) {
        static_cast<D*>(ctx)->BlockSetCallbacks(cb);
    }

    static void GetInfo(void* ctx, block_info_t* info) {
        static_cast<D*>(ctx)->BlockGetInfo(info);
    }

    static void Read(void* ctx, mx_handle_t vmo, uint64_t length, uint64_t vmo_offset,
                     uint64_t dev_offset, void* cookie) {
        static_cast<D*>(ctx)->BlockRead(vmo, length, vmo_offset, dev_offset, cookie);
    }

    static void Write(void* ctx, mx_handle_t vmo, uint64_t length, uint64_t vmo_offset,
                      uint64_t dev_offset, void* cookie) {
        static_cast<D*>(ctx)->BlockWrite(vmo, length, vmo_offset, dev_offset, cookie);
    }

    block_protocol_ops_t ops_ = {};
};

}  // namespace ddk
