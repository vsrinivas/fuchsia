// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// WARNING: THIS FILE IS MACHINE GENERATED. DO NOT EDIT.
//          MODIFY system/fidl/protocols/block.banjo INSTEAD.

#pragma once

#include <ddk/protocol/block.h>
#include <ddktl/device-internal.h>
#include <zircon/assert.h>
#include <zircon/compiler.h>
#include <zircon/device/block.h>
#include <zircon/types.h>

#include "block-internal.h"

// DDK block-impl-protocol support
//
// :: Proxies ::
//
// ddk::BlockImplProtocolProxy is a simple wrapper around
// block_impl_protocol_t. It does not own the pointers passed to it
//
// :: Mixins ::
//
// ddk::BlockImplProtocol is a mixin class that simplifies writing DDK drivers
// that implement the block-impl protocol. It doesn't set the base protocol.
//
// :: Examples ::
//
// // A driver that implements a ZX_PROTOCOL_BLOCK_IMPL device.
// class BlockImplDevice {
// using BlockImplDeviceType = ddk::Device<BlockImplDevice, /* ddk mixins */>;
//
// class BlockImplDevice : public BlockImplDeviceType,
//                         public ddk::BlockImplProtocol<BlockImplDevice> {
//   public:
//     BlockImplDevice(zx_device_t* parent)
//         : BlockImplDeviceType("my-block-impl-protocol-device", parent) {}
//
//     void BlockImplQuery(block_info_t* out_info, size_t* out_block_op_size);
//
//     void BlockImplQueue(block_op_t* txn, block_impl_queue_callback callback, void* cookie);
//
//     zx_status_t BlockImplGetStats(const void* cmd_buffer, size_t cmd_size, void*
//     out_reply_buffer, size_t reply_size, size_t* out_reply_actual);
//
//     ...
// };

namespace ddk {

template <typename D>
class BlockImplProtocol : public internal::base_protocol {
public:
    BlockImplProtocol() {
        internal::CheckBlockImplProtocolSubclass<D>();
        ops_.query = BlockImplQuery;
        ops_.queue = BlockImplQueue;
        ops_.get_stats = BlockImplGetStats;

        // Can only inherit from one base_protocol implementation.
        ZX_ASSERT(ddk_proto_id_ == 0);
        ddk_proto_id_ = ZX_PROTOCOL_BLOCK_IMPL;
        ddk_proto_ops_ = &ops_;
    }

protected:
    block_impl_protocol_ops_t ops_ = {};

private:
    // Obtain the parameters of the block device (block_info_t) and
    // the required size of block_txn_t.  The block_txn_t's submitted
    // via queue() must have block_op_size_out - sizeof(block_op_t) bytes
    // available at the end of the structure for the use of the driver.
    static void BlockImplQuery(void* ctx, block_info_t* out_info, size_t* out_block_op_size) {
        static_cast<D*>(ctx)->BlockImplQuery(out_info, out_block_op_size);
    }
    // Submit an IO request for processing.  Success or failure will
    // be reported via the completion_cb() in the block_op_t.  This
    // callback may be called before the queue() method returns.
    static void BlockImplQueue(void* ctx, block_op_t* txn, block_impl_queue_callback callback,
                               void* cookie) {
        static_cast<D*>(ctx)->BlockImplQueue(txn, callback, cookie);
    }
    static zx_status_t BlockImplGetStats(void* ctx, const void* cmd_buffer, size_t cmd_size,
                                         void* out_reply_buffer, size_t reply_size,
                                         size_t* out_reply_actual) {
        return static_cast<D*>(ctx)->BlockImplGetStats(cmd_buffer, cmd_size, out_reply_buffer,
                                                       reply_size, out_reply_actual);
    }
};

class BlockImplProtocolProxy {
public:
    BlockImplProtocolProxy() : ops_(nullptr), ctx_(nullptr) {}
    BlockImplProtocolProxy(const block_impl_protocol_t* proto)
        : ops_(proto->ops), ctx_(proto->ctx) {}

    void GetProto(block_impl_protocol_t* proto) {
        proto->ctx = ctx_;
        proto->ops = ops_;
    }
    bool is_valid() { return ops_ != nullptr; }
    void clear() {
        ctx_ = nullptr;
        ops_ = nullptr;
    }
    // Obtain the parameters of the block device (block_info_t) and
    // the required size of block_txn_t.  The block_txn_t's submitted
    // via queue() must have block_op_size_out - sizeof(block_op_t) bytes
    // available at the end of the structure for the use of the driver.
    void Query(block_info_t* out_info, size_t* out_block_op_size) {
        ops_->query(ctx_, out_info, out_block_op_size);
    }
    // Submit an IO request for processing.  Success or failure will
    // be reported via the completion_cb() in the block_op_t.  This
    // callback may be called before the queue() method returns.
    void Queue(block_op_t* txn, block_impl_queue_callback callback, void* cookie) {
        ops_->queue(ctx_, txn, callback, cookie);
    }
    zx_status_t GetStats(const void* cmd_buffer, size_t cmd_size, void* out_reply_buffer,
                         size_t reply_size, size_t* out_reply_actual) {
        return ops_->get_stats(ctx_, cmd_buffer, cmd_size, out_reply_buffer, reply_size,
                               out_reply_actual);
    }

private:
    block_impl_protocol_ops_t* ops_;
    void* ctx_;
};

} // namespace ddk
