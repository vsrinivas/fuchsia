// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <ddk/protocol/bad-block.h>
#include <ddktl/device-internal.h>

#include "bad-block-internal.h"

// DDK bad-block protocol support.
//
// :: Proxies ::
//
// ddk::BadBlockProtocolProxy is a simple wrappers around bad_block_protocol_t. It does
// not own the pointers passed to it.
//
// :: Mixins ::
//
// ddk::BadBlockable is a mixin class that simplifies writing DDK drivers that
// implement the bad-block protocol. It does not set the base protocol.
//
// :: Examples ::
//
// // A driver that implements a ZX_PROTOCOL_BAD_BLOCK device.
// class BadBlockDevice;
// using BadBlockDeviceType = ddk::Device<BadBlockDevice, /* ddk mixins */>;
//
// class BadBlockDevice : public BadBlockDeviceType,
//                        public ddk::BadBlockable<BadBlockDevice> {
//   public:
//     BadBlockDevice(zx_device_t* parent)
//       : BadBlockDeviceType("my-bad-block-device", parent) {}
//
//     zx_status_t GetBadBlockList(uint32_t* bad_block_list, uint32_t bad_block_list_len,
//                                 uint32_t* bad_block_count);
//     zx_status_t IsBlockBad(uint32_t block, bool* is_bad);
//     zx_status_t MarkBlockBad(uint32_t block);
//
//     ...
// };

namespace ddk {

template <typename D>
class BadBlockable : public internal::base_mixin {
public:
    BadBlockable() {
        internal::CheckBadBlockable<D>();
        bad_block_proto_ops_.get_bad_block_list = GetBadBlockList;
        bad_block_proto_ops_.mark_block_bad = MarkBlockBad;
    }

protected:
    bad_block_protocol_ops_t bad_block_proto_ops_ = {};

private:
    static zx_status_t GetBadBlockList(void* ctx, uint32_t* bad_block_list,
                                       uint32_t bad_block_list_len, uint32_t* bad_block_count) {
        return static_cast<D*>(ctx)->GetBadBlockList(bad_block_list, bad_block_list_len,
                                                      bad_block_count);
    }

    static zx_status_t MarkBlockBad(void* ctx, uint32_t block) {
        return static_cast<D*>(ctx)->MarkBlockBad(block);
    }
};

class BadBlockProtocolProxy {
public:
    BadBlockProtocolProxy(bad_block_protocol_t* proto)
        : ops_(proto->ops), ctx_(proto->ctx) {}

    zx_status_t GetBadBlockList(uint32_t* bad_block_list, uint32_t bad_block_list_len,
                                uint32_t* bad_block_count) {
        return ops_->get_bad_block_list(ctx_, bad_block_list, bad_block_list_len, bad_block_count);
    }

    zx_status_t MarkBlockBad(uint32_t block) {
        return ops_->mark_block_bad(ctx_, block);
    }

private:
    bad_block_protocol_ops_t* ops_;
    void* ctx_;
};

} // namespace ddk
