// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// WARNING: THIS FILE IS MACHINE GENERATED. DO NOT EDIT.
// Generated from the banjo.examples.syzkaller.protocol.multiple.returns banjo file

#pragma once

#include <banjo/examples/syzkaller/protocol/multiple/returns.h>
#include <ddk/driver.h>
#include <ddktl/device-internal.h>
#include <lib/zx/handle.h>
#include <zircon/assert.h>
#include <zircon/compiler.h>
#include <zircon/types.h>

#include "returns-internal.h"

// DDK returns-protocol support
//
// :: Proxies ::
//
// ddk::ApiProtocolClient is a simple wrapper around
// api_protocol_t. It does not own the pointers passed to it.
//
// :: Mixins ::
//
// ddk::ApiProtocol is a mixin class that simplifies writing DDK drivers
// that implement the api protocol. It doesn't set the base protocol.
//
// :: Examples ::
//
// // A driver that implements a ZX_PROTOCOL_API device.
// class ApiDevice;
// using ApiDeviceType = ddk::Device<ApiDevice, /* ddk mixins */>;
//
// class ApiDevice : public ApiDeviceType,
//                      public ddk::ApiProtocol<ApiDevice> {
//   public:
//     ApiDevice(zx_device_t* parent)
//         : ApiDeviceType(parent) {}
//
//     zx_status_t ApiUsize(size_t sz, size_t* out_sz_1);
//
//     zx_status_t ApiBool(bool b, bool* out_b_1);
//
//     zx_status_t ApiInt8(int8_t i8, int8_t* out_i8_1);
//
//     zx_status_t ApiInt16(int16_t i16, int16_t* out_i16_1);
//
//     zx_status_t ApiInt32(int32_t i32, int32_t* out_i32_1);
//
//     zx_status_t ApiInt64(int64_t i64, int64_t* out_i64_1);
//
//     zx_status_t ApiUint8(uint8_t u8, uint8_t* out_u8_1);
//
//     zx_status_t ApiUint16(uint16_t u16, uint16_t* out_u16_1);
//
//     zx_status_t ApiUint32(uint32_t u32, uint32_t* out_u32_1);
//
//     zx_status_t ApiUint64(uint64_t u64, uint64_t* out_u64_1);
//
//     zx_status_t ApiHandle(zx::handle h, zx::handle* out_h_1);
//
//     ...
// };

namespace ddk {

template <typename D, typename Base = internal::base_mixin>
class ApiProtocol : public Base {
public:
    ApiProtocol() {
        internal::CheckApiProtocolSubclass<D>();
        api_protocol_ops_.usize = ApiUsize;
        api_protocol_ops_.bool = ApiBool;
        api_protocol_ops_.int8 = ApiInt8;
        api_protocol_ops_.int16 = ApiInt16;
        api_protocol_ops_.int32 = ApiInt32;
        api_protocol_ops_.int64 = ApiInt64;
        api_protocol_ops_.uint8 = ApiUint8;
        api_protocol_ops_.uint16 = ApiUint16;
        api_protocol_ops_.uint32 = ApiUint32;
        api_protocol_ops_.uint64 = ApiUint64;
        api_protocol_ops_.handle = ApiHandle;

        if constexpr (internal::is_base_proto<Base>::value) {
            auto dev = static_cast<D*>(this);
            // Can only inherit from one base_protocol implementation.
            ZX_ASSERT(dev->ddk_proto_id_ == 0);
            dev->ddk_proto_id_ = ZX_PROTOCOL_API;
            dev->ddk_proto_ops_ = &api_protocol_ops_;
        }
    }

protected:
    api_protocol_ops_t api_protocol_ops_ = {};

private:
    static zx_status_t ApiUsize(void* ctx, size_t sz, size_t* out_sz_1) {
        auto ret = static_cast<D*>(ctx)->ApiUsize(sz, out_sz_1);
        return ret;
    }
    static zx_status_t ApiBool(void* ctx, bool b, bool* out_b_1) {
        auto ret = static_cast<D*>(ctx)->ApiBool(b, out_b_1);
        return ret;
    }
    static zx_status_t ApiInt8(void* ctx, int8_t i8, int8_t* out_i8_1) {
        auto ret = static_cast<D*>(ctx)->ApiInt8(i8, out_i8_1);
        return ret;
    }
    static zx_status_t ApiInt16(void* ctx, int16_t i16, int16_t* out_i16_1) {
        auto ret = static_cast<D*>(ctx)->ApiInt16(i16, out_i16_1);
        return ret;
    }
    static zx_status_t ApiInt32(void* ctx, int32_t i32, int32_t* out_i32_1) {
        auto ret = static_cast<D*>(ctx)->ApiInt32(i32, out_i32_1);
        return ret;
    }
    static zx_status_t ApiInt64(void* ctx, int64_t i64, int64_t* out_i64_1) {
        auto ret = static_cast<D*>(ctx)->ApiInt64(i64, out_i64_1);
        return ret;
    }
    static zx_status_t ApiUint8(void* ctx, uint8_t u8, uint8_t* out_u8_1) {
        auto ret = static_cast<D*>(ctx)->ApiUint8(u8, out_u8_1);
        return ret;
    }
    static zx_status_t ApiUint16(void* ctx, uint16_t u16, uint16_t* out_u16_1) {
        auto ret = static_cast<D*>(ctx)->ApiUint16(u16, out_u16_1);
        return ret;
    }
    static zx_status_t ApiUint32(void* ctx, uint32_t u32, uint32_t* out_u32_1) {
        auto ret = static_cast<D*>(ctx)->ApiUint32(u32, out_u32_1);
        return ret;
    }
    static zx_status_t ApiUint64(void* ctx, uint64_t u64, uint64_t* out_u64_1) {
        auto ret = static_cast<D*>(ctx)->ApiUint64(u64, out_u64_1);
        return ret;
    }
    static zx_status_t ApiHandle(void* ctx, zx_handle_t h, zx_handle_t* out_h_1) {
        zx::handle out_h_12;
        auto ret = static_cast<D*>(ctx)->ApiHandle(zx::handle(h), &out_h_12);
        *out_h_1 = out_h_12.release();
        return ret;
    }
};

class ApiProtocolClient {
public:
    ApiProtocolClient()
        : ops_(nullptr), ctx_(nullptr) {}
    ApiProtocolClient(const api_protocol_t* proto)
        : ops_(proto->ops), ctx_(proto->ctx) {}

    ApiProtocolClient(zx_device_t* parent) {
        api_protocol_t proto;
        if (device_get_protocol(parent, ZX_PROTOCOL_API, &proto) == ZX_OK) {
            ops_ = proto.ops;
            ctx_ = proto.ctx;
        } else {
            ops_ = nullptr;
            ctx_ = nullptr;
        }
    }

    // Create a ApiProtocolClient from the given parent device.
    //
    // If ZX_OK is returned, the created object will be initialized in |result|.
    static zx_status_t CreateFromDevice(zx_device_t* parent,
                                        ApiProtocolClient* result) {
        api_protocol_t proto;
        zx_status_t status = device_get_protocol(
                parent, ZX_PROTOCOL_API, &proto);
        if (status != ZX_OK) {
            return status;
        }
        *result = ApiProtocolClient(&proto);
        return ZX_OK;
    }

    void GetProto(api_protocol_t* proto) const {
        proto->ctx = ctx_;
        proto->ops = ops_;
    }
    bool is_valid() const {
        return ops_ != nullptr;
    }
    void clear() {
        ctx_ = nullptr;
        ops_ = nullptr;
    }

    zx_status_t Usize(size_t sz, size_t* out_sz_1) const {
        return ops_->usize(ctx_, sz, out_sz_1);
    }

    zx_status_t Bool(bool b, bool* out_b_1) const {
        return ops_->bool(ctx_, b, out_b_1);
    }

    zx_status_t Int8(int8_t i8, int8_t* out_i8_1) const {
        return ops_->int8(ctx_, i8, out_i8_1);
    }

    zx_status_t Int16(int16_t i16, int16_t* out_i16_1) const {
        return ops_->int16(ctx_, i16, out_i16_1);
    }

    zx_status_t Int32(int32_t i32, int32_t* out_i32_1) const {
        return ops_->int32(ctx_, i32, out_i32_1);
    }

    zx_status_t Int64(int64_t i64, int64_t* out_i64_1) const {
        return ops_->int64(ctx_, i64, out_i64_1);
    }

    zx_status_t Uint8(uint8_t u8, uint8_t* out_u8_1) const {
        return ops_->uint8(ctx_, u8, out_u8_1);
    }

    zx_status_t Uint16(uint16_t u16, uint16_t* out_u16_1) const {
        return ops_->uint16(ctx_, u16, out_u16_1);
    }

    zx_status_t Uint32(uint32_t u32, uint32_t* out_u32_1) const {
        return ops_->uint32(ctx_, u32, out_u32_1);
    }

    zx_status_t Uint64(uint64_t u64, uint64_t* out_u64_1) const {
        return ops_->uint64(ctx_, u64, out_u64_1);
    }

    zx_status_t Handle(zx::handle h, zx::handle* out_h_1) const {
        return ops_->handle(ctx_, h.release(), out_h_1->reset_and_get_address());
    }

private:
    api_protocol_ops_t* ops_;
    void* ctx_;
};

} // namespace ddk
