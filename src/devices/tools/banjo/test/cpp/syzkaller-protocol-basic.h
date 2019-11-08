// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// WARNING: THIS FILE IS MACHINE GENERATED. DO NOT EDIT.
// Generated from the banjo.examples.syzkaller.protocol.basic banjo file

#pragma once

#include <banjo/examples/syzkaller/protocol/basic.h>
#include <ddk/driver.h>
#include <ddktl/device-internal.h>
#include <lib/zx/handle.h>
#include <zircon/assert.h>
#include <zircon/compiler.h>
#include <zircon/types.h>

#include "basic-internal.h"

// DDK basic-protocol support
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
//     size_t ApiUsize(size_t sz);
//
//     bool ApiBool(bool b);
//
//     int8_t ApiInt8(int8_t i8);
//
//     int16_t ApiInt16(int16_t i16);
//
//     int32_t ApiInt32(int32_t i32);
//
//     int64_t ApiInt64(int64_t i64);
//
//     uint8_t ApiUint8(uint8_t u8);
//
//     uint16_t ApiUint16(uint16_t u16);
//
//     uint32_t ApiUint32(uint32_t u32);
//
//     uint64_t ApiUint64(uint64_t u64);
//
//     void ApiHandle(zx::handle h);
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
    static size_t ApiUsize(void* ctx, size_t sz) {
        auto ret = static_cast<D*>(ctx)->ApiUsize(sz);
        return ret;
    }
    static bool ApiBool(void* ctx, bool b) {
        auto ret = static_cast<D*>(ctx)->ApiBool(b);
        return ret;
    }
    static int8_t ApiInt8(void* ctx, int8_t i8) {
        auto ret = static_cast<D*>(ctx)->ApiInt8(i8);
        return ret;
    }
    static int16_t ApiInt16(void* ctx, int16_t i16) {
        auto ret = static_cast<D*>(ctx)->ApiInt16(i16);
        return ret;
    }
    static int32_t ApiInt32(void* ctx, int32_t i32) {
        auto ret = static_cast<D*>(ctx)->ApiInt32(i32);
        return ret;
    }
    static int64_t ApiInt64(void* ctx, int64_t i64) {
        auto ret = static_cast<D*>(ctx)->ApiInt64(i64);
        return ret;
    }
    static uint8_t ApiUint8(void* ctx, uint8_t u8) {
        auto ret = static_cast<D*>(ctx)->ApiUint8(u8);
        return ret;
    }
    static uint16_t ApiUint16(void* ctx, uint16_t u16) {
        auto ret = static_cast<D*>(ctx)->ApiUint16(u16);
        return ret;
    }
    static uint32_t ApiUint32(void* ctx, uint32_t u32) {
        auto ret = static_cast<D*>(ctx)->ApiUint32(u32);
        return ret;
    }
    static uint64_t ApiUint64(void* ctx, uint64_t u64) {
        auto ret = static_cast<D*>(ctx)->ApiUint64(u64);
        return ret;
    }
    static void ApiHandle(void* ctx, zx_handle_t h) {
        static_cast<D*>(ctx)->ApiHandle(zx::handle(h));
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

    size_t Usize(size_t sz) const {
        return ops_->usize(ctx_, sz);
    }

    bool Bool(bool b) const {
        return ops_->bool(ctx_, b);
    }

    int8_t Int8(int8_t i8) const {
        return ops_->int8(ctx_, i8);
    }

    int16_t Int16(int16_t i16) const {
        return ops_->int16(ctx_, i16);
    }

    int32_t Int32(int32_t i32) const {
        return ops_->int32(ctx_, i32);
    }

    int64_t Int64(int64_t i64) const {
        return ops_->int64(ctx_, i64);
    }

    uint8_t Uint8(uint8_t u8) const {
        return ops_->uint8(ctx_, u8);
    }

    uint16_t Uint16(uint16_t u16) const {
        return ops_->uint16(ctx_, u16);
    }

    uint32_t Uint32(uint32_t u32) const {
        return ops_->uint32(ctx_, u32);
    }

    uint64_t Uint64(uint64_t u64) const {
        return ops_->uint64(ctx_, u64);
    }

    void Handle(zx::handle h) const {
        ops_->handle(ctx_, h.release());
    }

private:
    api_protocol_ops_t* ops_;
    void* ctx_;
};

} // namespace ddk
