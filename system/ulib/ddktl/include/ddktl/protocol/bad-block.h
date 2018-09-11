// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// WARNING: THIS FILE IS MACHINE GENERATED. DO NOT EDIT.
//          MODIFY system/fidl/protocols/bad_block.banjo INSTEAD.

#pragma once

#include <ddk/protocol/bad-block.h>
#include <ddktl/device-internal.h>
#include <zircon/assert.h>
#include <zircon/compiler.h>
#include <zircon/types.h>

#include "bad-block-internal.h"

// DDK bad-block-protocol support
//
// :: Proxies ::
//
// ddk::BadBlockProtocolProxy is a simple wrapper around
// bad_block_protocol_t. It does not own the pointers passed to it
//
// :: Mixins ::
//
// ddk::BadBlockProtocol is a mixin class that simplifies writing DDK drivers
// that implement the bad-block protocol. It doesn't set the base protocol.
//
// :: Examples ::
//
// // A driver that implements a ZX_PROTOCOL_BAD_BLOCK device.
// class BadBlockDevice {
// using BadBlockDeviceType = ddk::Device<BadBlockDevice, /* ddk mixins */>;
//
// class BadBlockDevice : public BadBlockDeviceType,
//                        public ddk::BadBlockProtocol<BadBlockDevice> {
//   public:
//     BadBlockDevice(zx_device_t* parent)
//         : BadBlockDeviceType("my-bad-block-protocol-device", parent) {}
//
//     zx_status_t BadBlockGetBadBlockList(uint32_t* out_bad_blocks_list, size_t bad_blocks_count,
//     size_t* out_bad_blocks_actual);
//
//     zx_status_t BadBlockMarkBlockBad(uint32_t block);
//
//     ...
// };

namespace ddk {

template <typename D>
class BadBlockProtocol : public internal::base_mixin {
public:
    BadBlockProtocol() {
        internal::CheckBadBlockProtocolSubclass<D>();
        bad_block_protocol_ops_.get_bad_block_list = BadBlockGetBadBlockList;
        bad_block_protocol_ops_.mark_block_bad = BadBlockMarkBlockBad;
    }

protected:
    bad_block_protocol_ops_t bad_block_protocol_ops_ = {};

private:
    // Fills in |bad_blocks| with a list of bad blocks, up until
    // |bad_blocks_count|. The order of blocks is undefined.
    // |bad_blocks_actual| will be filled in with the actual number of bad
    // blocks. It is recommended to first make call with |bad_blocks_count|
    // equal to 0 in order to determine how large the |bad_blocks| is.
    static zx_status_t BadBlockGetBadBlockList(void* ctx, uint32_t* out_bad_blocks_list,
                                               size_t bad_blocks_count,
                                               size_t* out_bad_blocks_actual) {
        return static_cast<D*>(ctx)->BadBlockGetBadBlockList(out_bad_blocks_list, bad_blocks_count,
                                                             out_bad_blocks_actual);
    }
    // Sets |block| as bad. If block is already marked bad, it has no effect.
    static zx_status_t BadBlockMarkBlockBad(void* ctx, uint32_t block) {
        return static_cast<D*>(ctx)->BadBlockMarkBlockBad(block);
    }
};

class BadBlockProtocolProxy {
public:
    BadBlockProtocolProxy() : ops_(nullptr), ctx_(nullptr) {}
    BadBlockProtocolProxy(const bad_block_protocol_t* proto) : ops_(proto->ops), ctx_(proto->ctx) {}

    void GetProto(bad_block_protocol_t* proto) {
        proto->ctx = ctx_;
        proto->ops = ops_;
    }
    bool is_valid() { return ops_ != nullptr; }
    void clear() {
        ctx_ = nullptr;
        ops_ = nullptr;
    }
    // Fills in |bad_blocks| with a list of bad blocks, up until
    // |bad_blocks_count|. The order of blocks is undefined.
    // |bad_blocks_actual| will be filled in with the actual number of bad
    // blocks. It is recommended to first make call with |bad_blocks_count|
    // equal to 0 in order to determine how large the |bad_blocks| is.
    zx_status_t GetBadBlockList(uint32_t* out_bad_blocks_list, size_t bad_blocks_count,
                                size_t* out_bad_blocks_actual) {
        return ops_->get_bad_block_list(ctx_, out_bad_blocks_list, bad_blocks_count,
                                        out_bad_blocks_actual);
    }
    // Sets |block| as bad. If block is already marked bad, it has no effect.
    zx_status_t MarkBlockBad(uint32_t block) { return ops_->mark_block_bad(ctx_, block); }

private:
    bad_block_protocol_ops_t* ops_;
    void* ctx_;
};

} // namespace ddk
