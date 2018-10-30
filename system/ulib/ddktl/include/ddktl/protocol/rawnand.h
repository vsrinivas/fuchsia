// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// WARNING: THIS FILE IS MACHINE GENERATED. DO NOT EDIT.
//          MODIFY system/fidl/protocols/rawnand.banjo INSTEAD.

#pragma once

#include <ddk/protocol/rawnand.h>
#include <ddktl/device-internal.h>
#include <zircon/assert.h>
#include <zircon/compiler.h>
#include <zircon/device/nand.h>
#include <zircon/types.h>

#include "rawnand-internal.h"

// DDK raw-nand-protocol support
//
// :: Proxies ::
//
// ddk::RawNandProtocolProxy is a simple wrapper around
// raw_nand_protocol_t. It does not own the pointers passed to it
//
// :: Mixins ::
//
// ddk::RawNandProtocol is a mixin class that simplifies writing DDK drivers
// that implement the raw-nand protocol. It doesn't set the base protocol.
//
// :: Examples ::
//
// // A driver that implements a ZX_PROTOCOL_RAW_NAND device.
// class RawNandDevice {
// using RawNandDeviceType = ddk::Device<RawNandDevice, /* ddk mixins */>;
//
// class RawNandDevice : public RawNandDeviceType,
//                       public ddk::RawNandProtocol<RawNandDevice> {
//   public:
//     RawNandDevice(zx_device_t* parent)
//         : RawNandDeviceType("my-raw-nand-protocol-device", parent) {}
//
//     zx_status_t RawNandReadPageHwecc(uint32_t nandpage, void* out_data_buffer, size_t data_size,
//     size_t* out_data_actual, void* out_oob_buffer, size_t oob_size, size_t* out_oob_actual,
//     uint32_t* out_ecc_correct);
//
//     zx_status_t RawNandWritePageHwecc(const void* data_buffer, size_t data_size, const void*
//     oob_buffer, size_t oob_size, uint32_t nandpage);
//
//     zx_status_t RawNandEraseBlock(uint32_t nandpage);
//
//     zx_status_t RawNandGetNandInfo(nand_info_t* out_info);
//
//     ...
// };

namespace ddk {

template <typename D>
class RawNandProtocol : public internal::base_mixin {
public:
    RawNandProtocol() {
        internal::CheckRawNandProtocolSubclass<D>();
        raw_nand_protocol_ops_.read_page_hwecc = RawNandReadPageHwecc;
        raw_nand_protocol_ops_.write_page_hwecc = RawNandWritePageHwecc;
        raw_nand_protocol_ops_.erase_block = RawNandEraseBlock;
        raw_nand_protocol_ops_.get_nand_info = RawNandGetNandInfo;
    }

protected:
    raw_nand_protocol_ops_t raw_nand_protocol_ops_ = {};

private:
    // Read one nand page with hwecc.
    static zx_status_t RawNandReadPageHwecc(void* ctx, uint32_t nandpage, void* out_data_buffer,
                                            size_t data_size, size_t* out_data_actual,
                                            void* out_oob_buffer, size_t oob_size,
                                            size_t* out_oob_actual, uint32_t* out_ecc_correct) {
        return static_cast<D*>(ctx)->RawNandReadPageHwecc(nandpage, out_data_buffer, data_size,
                                                          out_data_actual, out_oob_buffer, oob_size,
                                                          out_oob_actual, out_ecc_correct);
    }
    // Write one nand page with hwecc.
    static zx_status_t RawNandWritePageHwecc(void* ctx, const void* data_buffer, size_t data_size,
                                             const void* oob_buffer, size_t oob_size,
                                             uint32_t nandpage) {
        return static_cast<D*>(ctx)->RawNandWritePageHwecc(data_buffer, data_size, oob_buffer,
                                                           oob_size, nandpage);
    }
    // Erase nand block.
    static zx_status_t RawNandEraseBlock(void* ctx, uint32_t nandpage) {
        return static_cast<D*>(ctx)->RawNandEraseBlock(nandpage);
    }
    static zx_status_t RawNandGetNandInfo(void* ctx, nand_info_t* out_info) {
        return static_cast<D*>(ctx)->RawNandGetNandInfo(out_info);
    }
};

class RawNandProtocolProxy {
public:
    RawNandProtocolProxy() : ops_(nullptr), ctx_(nullptr) {}
    RawNandProtocolProxy(const raw_nand_protocol_t* proto) : ops_(proto->ops), ctx_(proto->ctx) {}

    void GetProto(raw_nand_protocol_t* proto) {
        proto->ctx = ctx_;
        proto->ops = ops_;
    }
    bool is_valid() { return ops_ != nullptr; }
    void clear() {
        ctx_ = nullptr;
        ops_ = nullptr;
    }
    // Read one nand page with hwecc.
    zx_status_t ReadPageHwecc(uint32_t nandpage, void* out_data_buffer, size_t data_size,
                              size_t* out_data_actual, void* out_oob_buffer, size_t oob_size,
                              size_t* out_oob_actual, uint32_t* out_ecc_correct) {
        return ops_->read_page_hwecc(ctx_, nandpage, out_data_buffer, data_size, out_data_actual,
                                     out_oob_buffer, oob_size, out_oob_actual, out_ecc_correct);
    }
    // Write one nand page with hwecc.
    zx_status_t WritePageHwecc(const void* data_buffer, size_t data_size, const void* oob_buffer,
                               size_t oob_size, uint32_t nandpage) {
        return ops_->write_page_hwecc(ctx_, data_buffer, data_size, oob_buffer, oob_size, nandpage);
    }
    // Erase nand block.
    zx_status_t EraseBlock(uint32_t nandpage) { return ops_->erase_block(ctx_, nandpage); }
    zx_status_t GetNandInfo(nand_info_t* out_info) { return ops_->get_nand_info(ctx_, out_info); }

private:
    raw_nand_protocol_ops_t* ops_;
    void* ctx_;
};

} // namespace ddk
