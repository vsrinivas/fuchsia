// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// WARNING: THIS FILE IS MACHINE GENERATED. DO NOT EDIT.
// Generated from the banjo.examples.syzkaller.protocol.array banjo file

#pragma once

#include <banjo/examples/syzkaller/protocol/array.h>
#include <ddk/driver.h>
#include <ddktl/device-internal.h>
#include <zircon/assert.h>
#include <zircon/compiler.h>
#include <zircon/types.h>

#include "array-internal.h"

// DDK array-protocol support
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
//     zx_status_t ApiVoidPtr(const void vptr[1]);
//
//     zx_status_t ApiVoidPtr(const void vptr[1]);
//
//     zx_status_t ApiVoidPtr(const void vptr[vptr_len], size_t vptr_len);
//
//     zx_status_t ApiVoidPtr(const void vptr[vptr_len], size_t vptr_len);
//
//     zx_status_t ApiUsize(const size_t sz[1]);
//
//     zx_status_t ApiUsize(const size_t sz[1]);
//
//     zx_status_t ApiUsize(const size_t sz[sz_len], size_t sz_len);
//
//     zx_status_t ApiUsize(const size_t sz[sz_len], size_t sz_len);
//
//     zx_status_t ApiBool(const bool b[1]);
//
//     zx_status_t ApiBool(const bool b[1]);
//
//     zx_status_t ApiBool(const bool b[b_len], size_t b_len);
//
//     zx_status_t ApiBool(const bool b[b_len], size_t b_len);
//
//     zx_status_t ApiInt8(const int8_t i8[1]);
//
//     zx_status_t ApiInt8(const int8_t i8[1]);
//
//     zx_status_t ApiInt8(const int8_t i8[i8_len], size_t i8_len);
//
//     zx_status_t ApiInt8(const int8_t i8[i8_len], size_t i8_len);
//
//     zx_status_t ApiInt16(const int16_t i16[1]);
//
//     zx_status_t ApiInt16(const int16_t i16[1]);
//
//     zx_status_t ApiInt16(const int16_t i16[i16_len], size_t i16_len);
//
//     zx_status_t ApiInt16(const int16_t i16[i16_len], size_t i16_len);
//
//     zx_status_t ApiInt32(const int32_t i32[1]);
//
//     zx_status_t ApiInt32(const int32_t i32[1]);
//
//     zx_status_t ApiInt32(const int32_t i32[i32_len], size_t i32_len);
//
//     zx_status_t ApiInt32(const int32_t i32[i32_len], size_t i32_len);
//
//     zx_status_t ApiInt64(const int64_t i64[1]);
//
//     zx_status_t ApiInt64(const int64_t i64[1]);
//
//     zx_status_t ApiInt64(const int64_t i64[i64_len], size_t i64_len);
//
//     zx_status_t ApiInt64(const int64_t i64[i64_len], size_t i64_len);
//
//     zx_status_t ApiUint8(const uint8_t u8[1]);
//
//     zx_status_t ApiUint8(const uint8_t u8[1]);
//
//     zx_status_t ApiUint8(const uint8_t u8[u8_len], size_t u8_len);
//
//     zx_status_t ApiUint8(const uint8_t u8[u8_len], size_t u8_len);
//
//     zx_status_t ApiUint16(const uint16_t u16[1]);
//
//     zx_status_t ApiUint16(const uint16_t u16[1]);
//
//     zx_status_t ApiUint16(const uint16_t u16[u16_len], size_t u16_len);
//
//     zx_status_t ApiUint16(const uint16_t u16[u16_len], size_t u16_len);
//
//     zx_status_t ApiUint32(const uint32_t u32[1]);
//
//     zx_status_t ApiUint32(const uint32_t u32[1]);
//
//     zx_status_t ApiUint32(const uint32_t u32[u32_len], size_t u32_len);
//
//     zx_status_t ApiUint32(const uint32_t u32[u32_len], size_t u32_len);
//
//     zx_status_t ApiUint64(const uint64_t u64[1]);
//
//     zx_status_t ApiUint64(const uint64_t u64[1]);
//
//     zx_status_t ApiUint64(const uint64_t u64[u64_len], size_t u64_len);
//
//     zx_status_t ApiUint64(const uint64_t u64[u64_len], size_t u64_len);
//
//     zx_status_t ApiHandle(const zx::handle h[1]);
//
//     zx_status_t ApiHandle(const zx::handle h[1]);
//
//     zx_status_t ApiHandle(const zx::handle h[h_len], size_t h_len);
//
//     zx_status_t ApiHandle(const zx::handle h[h_len], size_t h_len);
//
//     ...
// };

namespace ddk {

template <typename D, typename Base = internal::base_mixin>
class ApiProtocol : public Base {
public:
    ApiProtocol() {
        internal::CheckApiProtocolSubclass<D>();
        api_protocol_ops_.void_ptr = ApiVoidPtr;
        api_protocol_ops_.void_ptr = ApiVoidPtr;
        api_protocol_ops_.void_ptr = ApiVoidPtr;
        api_protocol_ops_.void_ptr = ApiVoidPtr;
        api_protocol_ops_.usize = ApiUsize;
        api_protocol_ops_.usize = ApiUsize;
        api_protocol_ops_.usize = ApiUsize;
        api_protocol_ops_.usize = ApiUsize;
        api_protocol_ops_.bool = ApiBool;
        api_protocol_ops_.bool = ApiBool;
        api_protocol_ops_.bool = ApiBool;
        api_protocol_ops_.bool = ApiBool;
        api_protocol_ops_.int8 = ApiInt8;
        api_protocol_ops_.int8 = ApiInt8;
        api_protocol_ops_.int8 = ApiInt8;
        api_protocol_ops_.int8 = ApiInt8;
        api_protocol_ops_.int16 = ApiInt16;
        api_protocol_ops_.int16 = ApiInt16;
        api_protocol_ops_.int16 = ApiInt16;
        api_protocol_ops_.int16 = ApiInt16;
        api_protocol_ops_.int32 = ApiInt32;
        api_protocol_ops_.int32 = ApiInt32;
        api_protocol_ops_.int32 = ApiInt32;
        api_protocol_ops_.int32 = ApiInt32;
        api_protocol_ops_.int64 = ApiInt64;
        api_protocol_ops_.int64 = ApiInt64;
        api_protocol_ops_.int64 = ApiInt64;
        api_protocol_ops_.int64 = ApiInt64;
        api_protocol_ops_.uint8 = ApiUint8;
        api_protocol_ops_.uint8 = ApiUint8;
        api_protocol_ops_.uint8 = ApiUint8;
        api_protocol_ops_.uint8 = ApiUint8;
        api_protocol_ops_.uint16 = ApiUint16;
        api_protocol_ops_.uint16 = ApiUint16;
        api_protocol_ops_.uint16 = ApiUint16;
        api_protocol_ops_.uint16 = ApiUint16;
        api_protocol_ops_.uint32 = ApiUint32;
        api_protocol_ops_.uint32 = ApiUint32;
        api_protocol_ops_.uint32 = ApiUint32;
        api_protocol_ops_.uint32 = ApiUint32;
        api_protocol_ops_.uint64 = ApiUint64;
        api_protocol_ops_.uint64 = ApiUint64;
        api_protocol_ops_.uint64 = ApiUint64;
        api_protocol_ops_.uint64 = ApiUint64;
        api_protocol_ops_.handle = ApiHandle;
        api_protocol_ops_.handle = ApiHandle;
        api_protocol_ops_.handle = ApiHandle;
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
    static zx_status_t ApiVoidPtr(void* ctx, const void vptr[1]) {
        auto ret = static_cast<D*>(ctx)->ApiVoidPtr(vptr);
        return ret;
    }
    static zx_status_t ApiVoidPtr(void* ctx, const void vptr[1]) {
        auto ret = static_cast<D*>(ctx)->ApiVoidPtr(vptr);
        return ret;
    }
    static zx_status_t ApiVoidPtr(void* ctx, const void vptr[vptr_len], size_t vptr_len) {
        auto ret = static_cast<D*>(ctx)->ApiVoidPtr(vptr, vptr_len);
        return ret;
    }
    static zx_status_t ApiVoidPtr(void* ctx, const void vptr[vptr_len], size_t vptr_len) {
        auto ret = static_cast<D*>(ctx)->ApiVoidPtr(vptr, vptr_len);
        return ret;
    }
    static zx_status_t ApiUsize(void* ctx, const size_t sz[1]) {
        auto ret = static_cast<D*>(ctx)->ApiUsize(sz);
        return ret;
    }
    static zx_status_t ApiUsize(void* ctx, const size_t sz[1]) {
        auto ret = static_cast<D*>(ctx)->ApiUsize(sz);
        return ret;
    }
    static zx_status_t ApiUsize(void* ctx, const size_t sz[sz_len], size_t sz_len) {
        auto ret = static_cast<D*>(ctx)->ApiUsize(sz, sz_len);
        return ret;
    }
    static zx_status_t ApiUsize(void* ctx, const size_t sz[sz_len], size_t sz_len) {
        auto ret = static_cast<D*>(ctx)->ApiUsize(sz, sz_len);
        return ret;
    }
    static zx_status_t ApiBool(void* ctx, const bool b[1]) {
        auto ret = static_cast<D*>(ctx)->ApiBool(b);
        return ret;
    }
    static zx_status_t ApiBool(void* ctx, const bool b[1]) {
        auto ret = static_cast<D*>(ctx)->ApiBool(b);
        return ret;
    }
    static zx_status_t ApiBool(void* ctx, const bool b[b_len], size_t b_len) {
        auto ret = static_cast<D*>(ctx)->ApiBool(b, b_len);
        return ret;
    }
    static zx_status_t ApiBool(void* ctx, const bool b[b_len], size_t b_len) {
        auto ret = static_cast<D*>(ctx)->ApiBool(b, b_len);
        return ret;
    }
    static zx_status_t ApiInt8(void* ctx, const int8_t i8[1]) {
        auto ret = static_cast<D*>(ctx)->ApiInt8(i8);
        return ret;
    }
    static zx_status_t ApiInt8(void* ctx, const int8_t i8[1]) {
        auto ret = static_cast<D*>(ctx)->ApiInt8(i8);
        return ret;
    }
    static zx_status_t ApiInt8(void* ctx, const int8_t i8[i8_len], size_t i8_len) {
        auto ret = static_cast<D*>(ctx)->ApiInt8(i8, i8_len);
        return ret;
    }
    static zx_status_t ApiInt8(void* ctx, const int8_t i8[i8_len], size_t i8_len) {
        auto ret = static_cast<D*>(ctx)->ApiInt8(i8, i8_len);
        return ret;
    }
    static zx_status_t ApiInt16(void* ctx, const int16_t i16[1]) {
        auto ret = static_cast<D*>(ctx)->ApiInt16(i16);
        return ret;
    }
    static zx_status_t ApiInt16(void* ctx, const int16_t i16[1]) {
        auto ret = static_cast<D*>(ctx)->ApiInt16(i16);
        return ret;
    }
    static zx_status_t ApiInt16(void* ctx, const int16_t i16[i16_len], size_t i16_len) {
        auto ret = static_cast<D*>(ctx)->ApiInt16(i16, i16_len);
        return ret;
    }
    static zx_status_t ApiInt16(void* ctx, const int16_t i16[i16_len], size_t i16_len) {
        auto ret = static_cast<D*>(ctx)->ApiInt16(i16, i16_len);
        return ret;
    }
    static zx_status_t ApiInt32(void* ctx, const int32_t i32[1]) {
        auto ret = static_cast<D*>(ctx)->ApiInt32(i32);
        return ret;
    }
    static zx_status_t ApiInt32(void* ctx, const int32_t i32[1]) {
        auto ret = static_cast<D*>(ctx)->ApiInt32(i32);
        return ret;
    }
    static zx_status_t ApiInt32(void* ctx, const int32_t i32[i32_len], size_t i32_len) {
        auto ret = static_cast<D*>(ctx)->ApiInt32(i32, i32_len);
        return ret;
    }
    static zx_status_t ApiInt32(void* ctx, const int32_t i32[i32_len], size_t i32_len) {
        auto ret = static_cast<D*>(ctx)->ApiInt32(i32, i32_len);
        return ret;
    }
    static zx_status_t ApiInt64(void* ctx, const int64_t i64[1]) {
        auto ret = static_cast<D*>(ctx)->ApiInt64(i64);
        return ret;
    }
    static zx_status_t ApiInt64(void* ctx, const int64_t i64[1]) {
        auto ret = static_cast<D*>(ctx)->ApiInt64(i64);
        return ret;
    }
    static zx_status_t ApiInt64(void* ctx, const int64_t i64[i64_len], size_t i64_len) {
        auto ret = static_cast<D*>(ctx)->ApiInt64(i64, i64_len);
        return ret;
    }
    static zx_status_t ApiInt64(void* ctx, const int64_t i64[i64_len], size_t i64_len) {
        auto ret = static_cast<D*>(ctx)->ApiInt64(i64, i64_len);
        return ret;
    }
    static zx_status_t ApiUint8(void* ctx, const uint8_t u8[1]) {
        auto ret = static_cast<D*>(ctx)->ApiUint8(u8);
        return ret;
    }
    static zx_status_t ApiUint8(void* ctx, const uint8_t u8[1]) {
        auto ret = static_cast<D*>(ctx)->ApiUint8(u8);
        return ret;
    }
    static zx_status_t ApiUint8(void* ctx, const uint8_t u8[u8_len], size_t u8_len) {
        auto ret = static_cast<D*>(ctx)->ApiUint8(u8, u8_len);
        return ret;
    }
    static zx_status_t ApiUint8(void* ctx, const uint8_t u8[u8_len], size_t u8_len) {
        auto ret = static_cast<D*>(ctx)->ApiUint8(u8, u8_len);
        return ret;
    }
    static zx_status_t ApiUint16(void* ctx, const uint16_t u16[1]) {
        auto ret = static_cast<D*>(ctx)->ApiUint16(u16);
        return ret;
    }
    static zx_status_t ApiUint16(void* ctx, const uint16_t u16[1]) {
        auto ret = static_cast<D*>(ctx)->ApiUint16(u16);
        return ret;
    }
    static zx_status_t ApiUint16(void* ctx, const uint16_t u16[u16_len], size_t u16_len) {
        auto ret = static_cast<D*>(ctx)->ApiUint16(u16, u16_len);
        return ret;
    }
    static zx_status_t ApiUint16(void* ctx, const uint16_t u16[u16_len], size_t u16_len) {
        auto ret = static_cast<D*>(ctx)->ApiUint16(u16, u16_len);
        return ret;
    }
    static zx_status_t ApiUint32(void* ctx, const uint32_t u32[1]) {
        auto ret = static_cast<D*>(ctx)->ApiUint32(u32);
        return ret;
    }
    static zx_status_t ApiUint32(void* ctx, const uint32_t u32[1]) {
        auto ret = static_cast<D*>(ctx)->ApiUint32(u32);
        return ret;
    }
    static zx_status_t ApiUint32(void* ctx, const uint32_t u32[u32_len], size_t u32_len) {
        auto ret = static_cast<D*>(ctx)->ApiUint32(u32, u32_len);
        return ret;
    }
    static zx_status_t ApiUint32(void* ctx, const uint32_t u32[u32_len], size_t u32_len) {
        auto ret = static_cast<D*>(ctx)->ApiUint32(u32, u32_len);
        return ret;
    }
    static zx_status_t ApiUint64(void* ctx, const uint64_t u64[1]) {
        auto ret = static_cast<D*>(ctx)->ApiUint64(u64);
        return ret;
    }
    static zx_status_t ApiUint64(void* ctx, const uint64_t u64[1]) {
        auto ret = static_cast<D*>(ctx)->ApiUint64(u64);
        return ret;
    }
    static zx_status_t ApiUint64(void* ctx, const uint64_t u64[u64_len], size_t u64_len) {
        auto ret = static_cast<D*>(ctx)->ApiUint64(u64, u64_len);
        return ret;
    }
    static zx_status_t ApiUint64(void* ctx, const uint64_t u64[u64_len], size_t u64_len) {
        auto ret = static_cast<D*>(ctx)->ApiUint64(u64, u64_len);
        return ret;
    }
    static zx_status_t ApiHandle(void* ctx, const zx_handle_t h[1]) {
        auto ret = static_cast<D*>(ctx)->ApiHandle(h);
        return ret;
    }
    static zx_status_t ApiHandle(void* ctx, const zx_handle_t h[1]) {
        auto ret = static_cast<D*>(ctx)->ApiHandle(h);
        return ret;
    }
    static zx_status_t ApiHandle(void* ctx, const zx_handle_t h[h_len], size_t h_len) {
        auto ret = static_cast<D*>(ctx)->ApiHandle(h, h_len);
        return ret;
    }
    static zx_status_t ApiHandle(void* ctx, const zx_handle_t h[h_len], size_t h_len) {
        auto ret = static_cast<D*>(ctx)->ApiHandle(h, h_len);
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

    zx_status_t VoidPtr(const void vptr[1]) const {
        return ops_->void_ptr(ctx_, vptr);
    }

    zx_status_t VoidPtr(const void vptr[1]) const {
        return ops_->void_ptr(ctx_, vptr);
    }

    zx_status_t VoidPtr(const void vptr[vptr_len], size_t vptr_len) const {
        return ops_->void_ptr(ctx_, vptr, vptr_len);
    }

    zx_status_t VoidPtr(const void vptr[vptr_len], size_t vptr_len) const {
        return ops_->void_ptr(ctx_, vptr, vptr_len);
    }

    zx_status_t Usize(const size_t sz[1]) const {
        return ops_->usize(ctx_, sz);
    }

    zx_status_t Usize(const size_t sz[1]) const {
        return ops_->usize(ctx_, sz);
    }

    zx_status_t Usize(const size_t sz[sz_len], size_t sz_len) const {
        return ops_->usize(ctx_, sz, sz_len);
    }

    zx_status_t Usize(const size_t sz[sz_len], size_t sz_len) const {
        return ops_->usize(ctx_, sz, sz_len);
    }

    zx_status_t Bool(const bool b[1]) const {
        return ops_->bool(ctx_, b);
    }

    zx_status_t Bool(const bool b[1]) const {
        return ops_->bool(ctx_, b);
    }

    zx_status_t Bool(const bool b[b_len], size_t b_len) const {
        return ops_->bool(ctx_, b, b_len);
    }

    zx_status_t Bool(const bool b[b_len], size_t b_len) const {
        return ops_->bool(ctx_, b, b_len);
    }

    zx_status_t Int8(const int8_t i8[1]) const {
        return ops_->int8(ctx_, i8);
    }

    zx_status_t Int8(const int8_t i8[1]) const {
        return ops_->int8(ctx_, i8);
    }

    zx_status_t Int8(const int8_t i8[i8_len], size_t i8_len) const {
        return ops_->int8(ctx_, i8, i8_len);
    }

    zx_status_t Int8(const int8_t i8[i8_len], size_t i8_len) const {
        return ops_->int8(ctx_, i8, i8_len);
    }

    zx_status_t Int16(const int16_t i16[1]) const {
        return ops_->int16(ctx_, i16);
    }

    zx_status_t Int16(const int16_t i16[1]) const {
        return ops_->int16(ctx_, i16);
    }

    zx_status_t Int16(const int16_t i16[i16_len], size_t i16_len) const {
        return ops_->int16(ctx_, i16, i16_len);
    }

    zx_status_t Int16(const int16_t i16[i16_len], size_t i16_len) const {
        return ops_->int16(ctx_, i16, i16_len);
    }

    zx_status_t Int32(const int32_t i32[1]) const {
        return ops_->int32(ctx_, i32);
    }

    zx_status_t Int32(const int32_t i32[1]) const {
        return ops_->int32(ctx_, i32);
    }

    zx_status_t Int32(const int32_t i32[i32_len], size_t i32_len) const {
        return ops_->int32(ctx_, i32, i32_len);
    }

    zx_status_t Int32(const int32_t i32[i32_len], size_t i32_len) const {
        return ops_->int32(ctx_, i32, i32_len);
    }

    zx_status_t Int64(const int64_t i64[1]) const {
        return ops_->int64(ctx_, i64);
    }

    zx_status_t Int64(const int64_t i64[1]) const {
        return ops_->int64(ctx_, i64);
    }

    zx_status_t Int64(const int64_t i64[i64_len], size_t i64_len) const {
        return ops_->int64(ctx_, i64, i64_len);
    }

    zx_status_t Int64(const int64_t i64[i64_len], size_t i64_len) const {
        return ops_->int64(ctx_, i64, i64_len);
    }

    zx_status_t Uint8(const uint8_t u8[1]) const {
        return ops_->uint8(ctx_, u8);
    }

    zx_status_t Uint8(const uint8_t u8[1]) const {
        return ops_->uint8(ctx_, u8);
    }

    zx_status_t Uint8(const uint8_t u8[u8_len], size_t u8_len) const {
        return ops_->uint8(ctx_, u8, u8_len);
    }

    zx_status_t Uint8(const uint8_t u8[u8_len], size_t u8_len) const {
        return ops_->uint8(ctx_, u8, u8_len);
    }

    zx_status_t Uint16(const uint16_t u16[1]) const {
        return ops_->uint16(ctx_, u16);
    }

    zx_status_t Uint16(const uint16_t u16[1]) const {
        return ops_->uint16(ctx_, u16);
    }

    zx_status_t Uint16(const uint16_t u16[u16_len], size_t u16_len) const {
        return ops_->uint16(ctx_, u16, u16_len);
    }

    zx_status_t Uint16(const uint16_t u16[u16_len], size_t u16_len) const {
        return ops_->uint16(ctx_, u16, u16_len);
    }

    zx_status_t Uint32(const uint32_t u32[1]) const {
        return ops_->uint32(ctx_, u32);
    }

    zx_status_t Uint32(const uint32_t u32[1]) const {
        return ops_->uint32(ctx_, u32);
    }

    zx_status_t Uint32(const uint32_t u32[u32_len], size_t u32_len) const {
        return ops_->uint32(ctx_, u32, u32_len);
    }

    zx_status_t Uint32(const uint32_t u32[u32_len], size_t u32_len) const {
        return ops_->uint32(ctx_, u32, u32_len);
    }

    zx_status_t Uint64(const uint64_t u64[1]) const {
        return ops_->uint64(ctx_, u64);
    }

    zx_status_t Uint64(const uint64_t u64[1]) const {
        return ops_->uint64(ctx_, u64);
    }

    zx_status_t Uint64(const uint64_t u64[u64_len], size_t u64_len) const {
        return ops_->uint64(ctx_, u64, u64_len);
    }

    zx_status_t Uint64(const uint64_t u64[u64_len], size_t u64_len) const {
        return ops_->uint64(ctx_, u64, u64_len);
    }

    zx_status_t Handle(const zx::handle h[1]) const {
        return ops_->handle(ctx_, h);
    }

    zx_status_t Handle(const zx::handle h[1]) const {
        return ops_->handle(ctx_, h);
    }

    zx_status_t Handle(const zx::handle h[h_len], size_t h_len) const {
        return ops_->handle(ctx_, h, h_len);
    }

    zx_status_t Handle(const zx::handle h[h_len], size_t h_len) const {
        return ops_->handle(ctx_, h, h_len);
    }

private:
    api_protocol_ops_t* ops_;
    void* ctx_;
};

} // namespace ddk
