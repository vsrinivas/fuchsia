// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <ddk/protocol/nand.h>
#include <ddktl/device-internal.h>
#include <zircon/assert.h>

#include "nand-internal.h"

// DDK nand protocol support.
//
// :: Proxies ::
//
// ddk::NandProtocolProxy is a simple wrappers around nand_protocol_t. It does
// not own the pointers passed to it.
//
// :: Mixins ::
//
// ddk::NandProtocol is a mixin class that simplifies writing DDK drivers that
// implement the nand protocol.
//
// :: Examples ::
//
// // A driver that implements a ZX_PROTOCOL_NAND device.
// class NandDevice;
// using NandDeviceType = ddk::Device<NandDevice, /* ddk mixins */>;
//
// class NandDevice : public NandDeviceType,
//                    public ddk::NandProtocol<NandDevice> {
//   public:
//     NandDevice(zx_device_t* parent)
//       : NandDeviceType("my-nand-device", parent) {}
//
//     void Query(nand_info_t* info_out, size_t* nand_op_size_out);
//     void Queue(nand_op_t* operation);
//     void GetBadBlockList(uint32_t* bad_blocks, uint32_t bad_block_len,
//                          uint32_t* num_bad_blocks);
//     ...
// };

namespace ddk {

template <typename D>
class NandProtocol : public internal::base_protocol {
public:
    NandProtocol() {
        internal::CheckNandProtocolSubclass<D>();
        nand_proto_ops_.query = Query;
        nand_proto_ops_.queue = Queue;
        nand_proto_ops_.get_bad_block_list = GetBadBlockList;

        // Can only inherit from one base_protocol implementation.
        ZX_ASSERT(ddk_proto_id_ == 0);
        ddk_proto_id_ = ZX_PROTOCOL_NAND;
        ddk_proto_ops_ = &nand_proto_ops_;
    }

protected:
    nand_protocol_ops_t nand_proto_ops_ = {};

private:
    static void Query(void* ctx, nand_info_t* info_out, size_t* nand_op_size_out) {
        static_cast<D*>(ctx)->Query(info_out, nand_op_size_out);
    }

    static void Queue(void* ctx, nand_op_t* operation) {
        static_cast<D*>(ctx)->Queue(operation);
    }

    static void GetBadBlockList(void* ctx, uint32_t* bad_blocks, uint32_t bad_block_len,
                                uint32_t* num_bad_blocks) {
        static_cast<D*>(ctx)->GetBadBlockList(bad_blocks, bad_block_len, num_bad_blocks);
    }
};

class NandProtocolProxy {
public:
    NandProtocolProxy(nand_protocol_t* proto)
        : ops_(proto->ops), ctx_(proto->ctx) {}

    void Query(nand_info_t* info_out, size_t* nand_op_size_out) {
        ops_->query(ctx_, info_out, nand_op_size_out);
    }

    void Queue(nand_op_t* operation) {
        ops_->queue(ctx_, operation);
    }

    void GetBadBlockList(uint32_t* bad_blocks, uint32_t bad_block_len,
                         uint32_t* num_bad_blocks) {
        ops_->get_bad_block_list(ctx_, bad_blocks, bad_block_len, num_bad_blocks);
    }

private:
    nand_protocol_ops_t* ops_;
    void* ctx_;
};

} // namespace ddk
