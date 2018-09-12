// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// WARNING: THIS FILE IS MACHINE GENERATED. DO NOT EDIT.
//          MODIFY system/fidl/protocols/sdio.banjo INSTEAD.

#pragma once

#include <ddk/protocol/sdio.h>
#include <ddktl/device-internal.h>
#include <zircon/assert.h>
#include <zircon/compiler.h>
#include <zircon/types.h>

#include "sdio-internal.h"

// DDK sdio-protocol support
//
// :: Proxies ::
//
// ddk::SdioProtocolProxy is a simple wrapper around
// sdio_protocol_t. It does not own the pointers passed to it
//
// :: Mixins ::
//
// ddk::SdioProtocol is a mixin class that simplifies writing DDK drivers
// that implement the sdio protocol. It doesn't set the base protocol.
//
// :: Examples ::
//
// // A driver that implements a ZX_PROTOCOL_SDIO device.
// class SdioDevice {
// using SdioDeviceType = ddk::Device<SdioDevice, /* ddk mixins */>;
//
// class SdioDevice : public SdioDeviceType,
//                    public ddk::SdioProtocol<SdioDevice> {
//   public:
//     SdioDevice(zx_device_t* parent)
//         : SdioDeviceType("my-sdio-protocol-device", parent) {}
//
//     zx_status_t SdioGetDevHwInfo(sdio_hw_info_t* out_hw_info);
//
//     zx_status_t SdioEnableFn(uint8_t fn_idx);
//
//     zx_status_t SdioDisableFn(uint8_t fn_idx);
//
//     zx_status_t SdioEnableFnIntr(uint8_t fn_idx);
//
//     zx_status_t SdioDisableFnIntr(uint8_t fn_idx);
//
//     zx_status_t SdioUpdateBlockSize(uint8_t fn_idx, uint16_t blk_sz, bool deflt);
//
//     zx_status_t SdioGetBlockSize(uint8_t fn_idx, uint16_t* out_cur_blk_size);
//
//     zx_status_t SdioDoRwTxn(uint8_t fn_idx, sdio_rw_txn_t* txn);
//
//     zx_status_t SdioDoRwByte(bool write, uint8_t fn_idx, uint32_t addr, uint8_t write_byte,
//     uint8_t* out_read_byte);
//
//     ...
// };

namespace ddk {

template <typename D>
class SdioProtocol : public internal::base_mixin {
public:
    SdioProtocol() {
        internal::CheckSdioProtocolSubclass<D>();
        sdio_protocol_ops_.get_dev_hw_info = SdioGetDevHwInfo;
        sdio_protocol_ops_.enable_fn = SdioEnableFn;
        sdio_protocol_ops_.disable_fn = SdioDisableFn;
        sdio_protocol_ops_.enable_fn_intr = SdioEnableFnIntr;
        sdio_protocol_ops_.disable_fn_intr = SdioDisableFnIntr;
        sdio_protocol_ops_.update_block_size = SdioUpdateBlockSize;
        sdio_protocol_ops_.get_block_size = SdioGetBlockSize;
        sdio_protocol_ops_.do_rw_txn = SdioDoRwTxn;
        sdio_protocol_ops_.do_rw_byte = SdioDoRwByte;
    }

protected:
    sdio_protocol_ops_t sdio_protocol_ops_ = {};

private:
    static zx_status_t SdioGetDevHwInfo(void* ctx, sdio_hw_info_t* out_hw_info) {
        return static_cast<D*>(ctx)->SdioGetDevHwInfo(out_hw_info);
    }
    static zx_status_t SdioEnableFn(void* ctx, uint8_t fn_idx) {
        return static_cast<D*>(ctx)->SdioEnableFn(fn_idx);
    }
    static zx_status_t SdioDisableFn(void* ctx, uint8_t fn_idx) {
        return static_cast<D*>(ctx)->SdioDisableFn(fn_idx);
    }
    static zx_status_t SdioEnableFnIntr(void* ctx, uint8_t fn_idx) {
        return static_cast<D*>(ctx)->SdioEnableFnIntr(fn_idx);
    }
    static zx_status_t SdioDisableFnIntr(void* ctx, uint8_t fn_idx) {
        return static_cast<D*>(ctx)->SdioDisableFnIntr(fn_idx);
    }
    static zx_status_t SdioUpdateBlockSize(void* ctx, uint8_t fn_idx, uint16_t blk_sz, bool deflt) {
        return static_cast<D*>(ctx)->SdioUpdateBlockSize(fn_idx, blk_sz, deflt);
    }
    static zx_status_t SdioGetBlockSize(void* ctx, uint8_t fn_idx, uint16_t* out_cur_blk_size) {
        return static_cast<D*>(ctx)->SdioGetBlockSize(fn_idx, out_cur_blk_size);
    }
    static zx_status_t SdioDoRwTxn(void* ctx, uint8_t fn_idx, sdio_rw_txn_t* txn) {
        return static_cast<D*>(ctx)->SdioDoRwTxn(fn_idx, txn);
    }
    static zx_status_t SdioDoRwByte(void* ctx, bool write, uint8_t fn_idx, uint32_t addr,
                                    uint8_t write_byte, uint8_t* out_read_byte) {
        return static_cast<D*>(ctx)->SdioDoRwByte(write, fn_idx, addr, write_byte, out_read_byte);
    }
};

class SdioProtocolProxy {
public:
    SdioProtocolProxy() : ops_(nullptr), ctx_(nullptr) {}
    SdioProtocolProxy(const sdio_protocol_t* proto) : ops_(proto->ops), ctx_(proto->ctx) {}

    void GetProto(sdio_protocol_t* proto) {
        proto->ctx = ctx_;
        proto->ops = ops_;
    }
    bool is_valid() { return ops_ != nullptr; }
    void clear() {
        ctx_ = nullptr;
        ops_ = nullptr;
    }
    zx_status_t GetDevHwInfo(sdio_hw_info_t* out_hw_info) {
        return ops_->get_dev_hw_info(ctx_, out_hw_info);
    }
    zx_status_t EnableFn(uint8_t fn_idx) { return ops_->enable_fn(ctx_, fn_idx); }
    zx_status_t DisableFn(uint8_t fn_idx) { return ops_->disable_fn(ctx_, fn_idx); }
    zx_status_t EnableFnIntr(uint8_t fn_idx) { return ops_->enable_fn_intr(ctx_, fn_idx); }
    zx_status_t DisableFnIntr(uint8_t fn_idx) { return ops_->disable_fn_intr(ctx_, fn_idx); }
    zx_status_t UpdateBlockSize(uint8_t fn_idx, uint16_t blk_sz, bool deflt) {
        return ops_->update_block_size(ctx_, fn_idx, blk_sz, deflt);
    }
    zx_status_t GetBlockSize(uint8_t fn_idx, uint16_t* out_cur_blk_size) {
        return ops_->get_block_size(ctx_, fn_idx, out_cur_blk_size);
    }
    zx_status_t DoRwTxn(uint8_t fn_idx, sdio_rw_txn_t* txn) {
        return ops_->do_rw_txn(ctx_, fn_idx, txn);
    }
    zx_status_t DoRwByte(bool write, uint8_t fn_idx, uint32_t addr, uint8_t write_byte,
                         uint8_t* out_read_byte) {
        return ops_->do_rw_byte(ctx_, write, fn_idx, addr, write_byte, out_read_byte);
    }

private:
    sdio_protocol_ops_t* ops_;
    void* ctx_;
};

} // namespace ddk
