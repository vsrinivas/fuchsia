// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// WARNING: THIS FILE IS MACHINE GENERATED. DO NOT EDIT.
// Generated from the banjo.examples.protocolprimitive banjo file

#pragma once

#include <banjo/examples/protocolprimitive/c/banjo.h>
#include <ddktl/device-internal.h>
#include <lib/ddk/device.h>
#include <lib/ddk/driver.h>
#include <zircon/assert.h>
#include <zircon/compiler.h>
#include <zircon/types.h>

#include "banjo-internal.h"

// DDK protocolprimitive-protocol support
//
// :: Proxies ::
//
// ddk::SynchronousPrimitiveProtocolClient is a simple wrapper around
// synchronous_primitive_protocol_t. It does not own the pointers passed to it.
//
// :: Mixins ::
//
// ddk::SynchronousPrimitiveProtocol is a mixin class that simplifies writing DDK drivers
// that implement the synchronous-primitive protocol. It doesn't set the base protocol.
//
// :: Examples ::
//
// // A driver that implements a ZX_PROTOCOL_SYNCHRONOUS_PRIMITIVE device.
// class SynchronousPrimitiveDevice;
// using SynchronousPrimitiveDeviceType = ddk::Device<SynchronousPrimitiveDevice, /* ddk mixins */>;
//
// class SynchronousPrimitiveDevice : public SynchronousPrimitiveDeviceType,
//                      public ddk::SynchronousPrimitiveProtocol<SynchronousPrimitiveDevice> {
//   public:
//     SynchronousPrimitiveDevice(zx_device_t* parent)
//         : SynchronousPrimitiveDeviceType(parent) {}
//
//     bool SynchronousPrimitiveBool(bool b, bool* out_b_2);
//
//     int8_t SynchronousPrimitiveInt8(int8_t i8, int8_t* out_i8_2);
//
//     int16_t SynchronousPrimitiveInt16(int16_t i16, int16_t* out_i16_2);
//
//     int32_t SynchronousPrimitiveInt32(int32_t i32, int32_t* out_i32_2);
//
//     int64_t SynchronousPrimitiveInt64(int64_t i64, int64_t* out_i64_2);
//
//     uint8_t SynchronousPrimitiveUint8(uint8_t u8, uint8_t* out_u8_2);
//
//     uint16_t SynchronousPrimitiveUint16(uint16_t u16, uint16_t* out_u16_2);
//
//     uint32_t SynchronousPrimitiveUint32(uint32_t u32, uint32_t* out_u32_2);
//
//     uint64_t SynchronousPrimitiveUint64(uint64_t u64, uint64_t* out_u64_2);
//
//     float SynchronousPrimitiveFloat32(float f32, float* out_f32_2);
//
//     double SynchronousPrimitiveFloat64(double u64, double* out_f64_2);
//
//     ...
// };
// :: Proxies ::
//
// ddk::AsyncPrimitiveProtocolClient is a simple wrapper around
// async_primitive_protocol_t. It does not own the pointers passed to it.
//
// :: Mixins ::
//
// ddk::AsyncPrimitiveProtocol is a mixin class that simplifies writing DDK drivers
// that implement the async-primitive protocol. It doesn't set the base protocol.
//
// :: Examples ::
//
// // A driver that implements a ZX_PROTOCOL_ASYNC_PRIMITIVE device.
// class AsyncPrimitiveDevice;
// using AsyncPrimitiveDeviceType = ddk::Device<AsyncPrimitiveDevice, /* ddk mixins */>;
//
// class AsyncPrimitiveDevice : public AsyncPrimitiveDeviceType,
//                      public ddk::AsyncPrimitiveProtocol<AsyncPrimitiveDevice> {
//   public:
//     AsyncPrimitiveDevice(zx_device_t* parent)
//         : AsyncPrimitiveDeviceType(parent) {}
//
//     void AsyncPrimitiveBool(bool b, async_primitive_bool_callback callback, void* cookie);
//
//     void AsyncPrimitiveInt8(int8_t i8, async_primitive_int8_callback callback, void* cookie);
//
//     void AsyncPrimitiveInt16(int16_t i16, async_primitive_int16_callback callback, void* cookie);
//
//     void AsyncPrimitiveInt32(int32_t i32, async_primitive_int32_callback callback, void* cookie);
//
//     void AsyncPrimitiveInt64(int64_t i64, async_primitive_int64_callback callback, void* cookie);
//
//     void AsyncPrimitiveUint8(uint8_t u8, async_primitive_uint8_callback callback, void* cookie);
//
//     void AsyncPrimitiveUint16(uint16_t u16, async_primitive_uint16_callback callback, void* cookie);
//
//     void AsyncPrimitiveUint32(uint32_t u32, async_primitive_uint32_callback callback, void* cookie);
//
//     void AsyncPrimitiveUint64(uint64_t u64, async_primitive_uint64_callback callback, void* cookie);
//
//     void AsyncPrimitiveFloat32(float f32, async_primitive_float32_callback callback, void* cookie);
//
//     void AsyncPrimitiveFloat64(double u64, async_primitive_float64_callback callback, void* cookie);
//
//     ...
// };

namespace ddk {

template <typename D, typename Base = internal::base_mixin>
class SynchronousPrimitiveProtocol : public Base {
public:
    SynchronousPrimitiveProtocol() {
        internal::CheckSynchronousPrimitiveProtocolSubclass<D>();
        synchronous_primitive_protocol_ops_.bool = SynchronousPrimitiveBool;
        synchronous_primitive_protocol_ops_.int8 = SynchronousPrimitiveInt8;
        synchronous_primitive_protocol_ops_.int16 = SynchronousPrimitiveInt16;
        synchronous_primitive_protocol_ops_.int32 = SynchronousPrimitiveInt32;
        synchronous_primitive_protocol_ops_.int64 = SynchronousPrimitiveInt64;
        synchronous_primitive_protocol_ops_.uint8 = SynchronousPrimitiveUint8;
        synchronous_primitive_protocol_ops_.uint16 = SynchronousPrimitiveUint16;
        synchronous_primitive_protocol_ops_.uint32 = SynchronousPrimitiveUint32;
        synchronous_primitive_protocol_ops_.uint64 = SynchronousPrimitiveUint64;
        synchronous_primitive_protocol_ops_.float32 = SynchronousPrimitiveFloat32;
        synchronous_primitive_protocol_ops_.float64 = SynchronousPrimitiveFloat64;

        if constexpr (internal::is_base_proto<Base>::value) {
            auto dev = static_cast<D*>(this);
            // Can only inherit from one base_protocol implementation.
            ZX_ASSERT(dev->ddk_proto_id_ == 0);
            dev->ddk_proto_id_ = ZX_PROTOCOL_SYNCHRONOUS_PRIMITIVE;
            dev->ddk_proto_ops_ = &synchronous_primitive_protocol_ops_;
        }
    }

protected:
    synchronous_primitive_protocol_ops_t synchronous_primitive_protocol_ops_ = {};

private:
    static bool SynchronousPrimitiveBool(void* ctx, bool b, bool* out_b_2) {
        auto ret = static_cast<D*>(ctx)->SynchronousPrimitiveBool(b, out_b_2);
        return ret;
    }
    static int8_t SynchronousPrimitiveInt8(void* ctx, int8_t i8, int8_t* out_i8_2) {
        auto ret = static_cast<D*>(ctx)->SynchronousPrimitiveInt8(i8, out_i8_2);
        return ret;
    }
    static int16_t SynchronousPrimitiveInt16(void* ctx, int16_t i16, int16_t* out_i16_2) {
        auto ret = static_cast<D*>(ctx)->SynchronousPrimitiveInt16(i16, out_i16_2);
        return ret;
    }
    static int32_t SynchronousPrimitiveInt32(void* ctx, int32_t i32, int32_t* out_i32_2) {
        auto ret = static_cast<D*>(ctx)->SynchronousPrimitiveInt32(i32, out_i32_2);
        return ret;
    }
    static int64_t SynchronousPrimitiveInt64(void* ctx, int64_t i64, int64_t* out_i64_2) {
        auto ret = static_cast<D*>(ctx)->SynchronousPrimitiveInt64(i64, out_i64_2);
        return ret;
    }
    static uint8_t SynchronousPrimitiveUint8(void* ctx, uint8_t u8, uint8_t* out_u8_2) {
        auto ret = static_cast<D*>(ctx)->SynchronousPrimitiveUint8(u8, out_u8_2);
        return ret;
    }
    static uint16_t SynchronousPrimitiveUint16(void* ctx, uint16_t u16, uint16_t* out_u16_2) {
        auto ret = static_cast<D*>(ctx)->SynchronousPrimitiveUint16(u16, out_u16_2);
        return ret;
    }
    static uint32_t SynchronousPrimitiveUint32(void* ctx, uint32_t u32, uint32_t* out_u32_2) {
        auto ret = static_cast<D*>(ctx)->SynchronousPrimitiveUint32(u32, out_u32_2);
        return ret;
    }
    static uint64_t SynchronousPrimitiveUint64(void* ctx, uint64_t u64, uint64_t* out_u64_2) {
        auto ret = static_cast<D*>(ctx)->SynchronousPrimitiveUint64(u64, out_u64_2);
        return ret;
    }
    static float SynchronousPrimitiveFloat32(void* ctx, float f32, float* out_f32_2) {
        auto ret = static_cast<D*>(ctx)->SynchronousPrimitiveFloat32(f32, out_f32_2);
        return ret;
    }
    static double SynchronousPrimitiveFloat64(void* ctx, double u64, double* out_f64_2) {
        auto ret = static_cast<D*>(ctx)->SynchronousPrimitiveFloat64(u64, out_f64_2);
        return ret;
    }
};

class SynchronousPrimitiveProtocolClient {
public:
    SynchronousPrimitiveProtocolClient()
        : ops_(nullptr), ctx_(nullptr) {}
    SynchronousPrimitiveProtocolClient(const synchronous_primitive_protocol_t* proto)
        : ops_(proto->ops), ctx_(proto->ctx) {}

    SynchronousPrimitiveProtocolClient(zx_device_t* parent) {
        synchronous_primitive_protocol_t proto;
        if (device_get_protocol(parent, ZX_PROTOCOL_SYNCHRONOUS_PRIMITIVE, &proto) == ZX_OK) {
            ops_ = proto.ops;
            ctx_ = proto.ctx;
        } else {
            ops_ = nullptr;
            ctx_ = nullptr;
        }
    }

    SynchronousPrimitiveProtocolClient(zx_device_t* parent, const char* fragment_name) {
        synchronous_primitive_protocol_t proto;
        if (device_get_fragment_protocol(parent, fragment_name, ZX_PROTOCOL_SYNCHRONOUS_PRIMITIVE, &proto) == ZX_OK) {
            ops_ = proto.ops;
            ctx_ = proto.ctx;
        } else {
            ops_ = nullptr;
            ctx_ = nullptr;
        }
    }

    // Create a SynchronousPrimitiveProtocolClient from the given parent device + "fragment".
    //
    // If ZX_OK is returned, the created object will be initialized in |result|.
    static zx_status_t CreateFromDevice(zx_device_t* parent,
                                        SynchronousPrimitiveProtocolClient* result) {
        synchronous_primitive_protocol_t proto;
        zx_status_t status = device_get_protocol(
                parent, ZX_PROTOCOL_SYNCHRONOUS_PRIMITIVE, &proto);
        if (status != ZX_OK) {
            return status;
        }
        *result = SynchronousPrimitiveProtocolClient(&proto);
        return ZX_OK;
    }

    // Create a SynchronousPrimitiveProtocolClient from the given parent device.
    //
    // If ZX_OK is returned, the created object will be initialized in |result|.
    static zx_status_t CreateFromDevice(zx_device_t* parent, const char* fragment_name,
                                        SynchronousPrimitiveProtocolClient* result) {
        synchronous_primitive_protocol_t proto;
        zx_status_t status = device_get_fragment_protocol(parent, fragment_name,
                                 ZX_PROTOCOL_SYNCHRONOUS_PRIMITIVE, &proto);
        if (status != ZX_OK) {
            return status;
        }
        *result = SynchronousPrimitiveProtocolClient(&proto);
        return ZX_OK;
    }

    void GetProto(synchronous_primitive_protocol_t* proto) const {
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

    bool Bool(bool b, bool* out_b_2) const {
        return ops_->bool(ctx_, b, out_b_2);
    }

    int8_t Int8(int8_t i8, int8_t* out_i8_2) const {
        return ops_->int8(ctx_, i8, out_i8_2);
    }

    int16_t Int16(int16_t i16, int16_t* out_i16_2) const {
        return ops_->int16(ctx_, i16, out_i16_2);
    }

    int32_t Int32(int32_t i32, int32_t* out_i32_2) const {
        return ops_->int32(ctx_, i32, out_i32_2);
    }

    int64_t Int64(int64_t i64, int64_t* out_i64_2) const {
        return ops_->int64(ctx_, i64, out_i64_2);
    }

    uint8_t Uint8(uint8_t u8, uint8_t* out_u8_2) const {
        return ops_->uint8(ctx_, u8, out_u8_2);
    }

    uint16_t Uint16(uint16_t u16, uint16_t* out_u16_2) const {
        return ops_->uint16(ctx_, u16, out_u16_2);
    }

    uint32_t Uint32(uint32_t u32, uint32_t* out_u32_2) const {
        return ops_->uint32(ctx_, u32, out_u32_2);
    }

    uint64_t Uint64(uint64_t u64, uint64_t* out_u64_2) const {
        return ops_->uint64(ctx_, u64, out_u64_2);
    }

    float Float32(float f32, float* out_f32_2) const {
        return ops_->float32(ctx_, f32, out_f32_2);
    }

    double Float64(double u64, double* out_f64_2) const {
        return ops_->float64(ctx_, u64, out_f64_2);
    }

private:
    const synchronous_primitive_protocol_ops_t* ops_;
    void* ctx_;
};

template <typename D, typename Base = internal::base_mixin>
class AsyncPrimitiveProtocol : public Base {
public:
    AsyncPrimitiveProtocol() {
        internal::CheckAsyncPrimitiveProtocolSubclass<D>();
        async_primitive_protocol_ops_.bool = AsyncPrimitiveBool;
        async_primitive_protocol_ops_.int8 = AsyncPrimitiveInt8;
        async_primitive_protocol_ops_.int16 = AsyncPrimitiveInt16;
        async_primitive_protocol_ops_.int32 = AsyncPrimitiveInt32;
        async_primitive_protocol_ops_.int64 = AsyncPrimitiveInt64;
        async_primitive_protocol_ops_.uint8 = AsyncPrimitiveUint8;
        async_primitive_protocol_ops_.uint16 = AsyncPrimitiveUint16;
        async_primitive_protocol_ops_.uint32 = AsyncPrimitiveUint32;
        async_primitive_protocol_ops_.uint64 = AsyncPrimitiveUint64;
        async_primitive_protocol_ops_.float32 = AsyncPrimitiveFloat32;
        async_primitive_protocol_ops_.float64 = AsyncPrimitiveFloat64;

        if constexpr (internal::is_base_proto<Base>::value) {
            auto dev = static_cast<D*>(this);
            // Can only inherit from one base_protocol implementation.
            ZX_ASSERT(dev->ddk_proto_id_ == 0);
            dev->ddk_proto_id_ = ZX_PROTOCOL_ASYNC_PRIMITIVE;
            dev->ddk_proto_ops_ = &async_primitive_protocol_ops_;
        }
    }

protected:
    async_primitive_protocol_ops_t async_primitive_protocol_ops_ = {};

private:
    static void AsyncPrimitiveBool(void* ctx, bool b, async_primitive_bool_callback callback, void* cookie) {
        static_cast<D*>(ctx)->AsyncPrimitiveBool(b, callback, cookie);
    }
    static void AsyncPrimitiveInt8(void* ctx, int8_t i8, async_primitive_int8_callback callback, void* cookie) {
        static_cast<D*>(ctx)->AsyncPrimitiveInt8(i8, callback, cookie);
    }
    static void AsyncPrimitiveInt16(void* ctx, int16_t i16, async_primitive_int16_callback callback, void* cookie) {
        static_cast<D*>(ctx)->AsyncPrimitiveInt16(i16, callback, cookie);
    }
    static void AsyncPrimitiveInt32(void* ctx, int32_t i32, async_primitive_int32_callback callback, void* cookie) {
        static_cast<D*>(ctx)->AsyncPrimitiveInt32(i32, callback, cookie);
    }
    static void AsyncPrimitiveInt64(void* ctx, int64_t i64, async_primitive_int64_callback callback, void* cookie) {
        static_cast<D*>(ctx)->AsyncPrimitiveInt64(i64, callback, cookie);
    }
    static void AsyncPrimitiveUint8(void* ctx, uint8_t u8, async_primitive_uint8_callback callback, void* cookie) {
        static_cast<D*>(ctx)->AsyncPrimitiveUint8(u8, callback, cookie);
    }
    static void AsyncPrimitiveUint16(void* ctx, uint16_t u16, async_primitive_uint16_callback callback, void* cookie) {
        static_cast<D*>(ctx)->AsyncPrimitiveUint16(u16, callback, cookie);
    }
    static void AsyncPrimitiveUint32(void* ctx, uint32_t u32, async_primitive_uint32_callback callback, void* cookie) {
        static_cast<D*>(ctx)->AsyncPrimitiveUint32(u32, callback, cookie);
    }
    static void AsyncPrimitiveUint64(void* ctx, uint64_t u64, async_primitive_uint64_callback callback, void* cookie) {
        static_cast<D*>(ctx)->AsyncPrimitiveUint64(u64, callback, cookie);
    }
    static void AsyncPrimitiveFloat32(void* ctx, float f32, async_primitive_float32_callback callback, void* cookie) {
        static_cast<D*>(ctx)->AsyncPrimitiveFloat32(f32, callback, cookie);
    }
    static void AsyncPrimitiveFloat64(void* ctx, double u64, async_primitive_float64_callback callback, void* cookie) {
        static_cast<D*>(ctx)->AsyncPrimitiveFloat64(u64, callback, cookie);
    }
};

class AsyncPrimitiveProtocolClient {
public:
    AsyncPrimitiveProtocolClient()
        : ops_(nullptr), ctx_(nullptr) {}
    AsyncPrimitiveProtocolClient(const async_primitive_protocol_t* proto)
        : ops_(proto->ops), ctx_(proto->ctx) {}

    AsyncPrimitiveProtocolClient(zx_device_t* parent) {
        async_primitive_protocol_t proto;
        if (device_get_protocol(parent, ZX_PROTOCOL_ASYNC_PRIMITIVE, &proto) == ZX_OK) {
            ops_ = proto.ops;
            ctx_ = proto.ctx;
        } else {
            ops_ = nullptr;
            ctx_ = nullptr;
        }
    }

    AsyncPrimitiveProtocolClient(zx_device_t* parent, const char* fragment_name) {
        async_primitive_protocol_t proto;
        if (device_get_fragment_protocol(parent, fragment_name, ZX_PROTOCOL_ASYNC_PRIMITIVE, &proto) == ZX_OK) {
            ops_ = proto.ops;
            ctx_ = proto.ctx;
        } else {
            ops_ = nullptr;
            ctx_ = nullptr;
        }
    }

    // Create a AsyncPrimitiveProtocolClient from the given parent device + "fragment".
    //
    // If ZX_OK is returned, the created object will be initialized in |result|.
    static zx_status_t CreateFromDevice(zx_device_t* parent,
                                        AsyncPrimitiveProtocolClient* result) {
        async_primitive_protocol_t proto;
        zx_status_t status = device_get_protocol(
                parent, ZX_PROTOCOL_ASYNC_PRIMITIVE, &proto);
        if (status != ZX_OK) {
            return status;
        }
        *result = AsyncPrimitiveProtocolClient(&proto);
        return ZX_OK;
    }

    // Create a AsyncPrimitiveProtocolClient from the given parent device.
    //
    // If ZX_OK is returned, the created object will be initialized in |result|.
    static zx_status_t CreateFromDevice(zx_device_t* parent, const char* fragment_name,
                                        AsyncPrimitiveProtocolClient* result) {
        async_primitive_protocol_t proto;
        zx_status_t status = device_get_fragment_protocol(parent, fragment_name,
                                 ZX_PROTOCOL_ASYNC_PRIMITIVE, &proto);
        if (status != ZX_OK) {
            return status;
        }
        *result = AsyncPrimitiveProtocolClient(&proto);
        return ZX_OK;
    }

    void GetProto(async_primitive_protocol_t* proto) const {
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

    void Bool(bool b, async_primitive_bool_callback callback, void* cookie) const {
        ops_->bool(ctx_, b, callback, cookie);
    }

    void Int8(int8_t i8, async_primitive_int8_callback callback, void* cookie) const {
        ops_->int8(ctx_, i8, callback, cookie);
    }

    void Int16(int16_t i16, async_primitive_int16_callback callback, void* cookie) const {
        ops_->int16(ctx_, i16, callback, cookie);
    }

    void Int32(int32_t i32, async_primitive_int32_callback callback, void* cookie) const {
        ops_->int32(ctx_, i32, callback, cookie);
    }

    void Int64(int64_t i64, async_primitive_int64_callback callback, void* cookie) const {
        ops_->int64(ctx_, i64, callback, cookie);
    }

    void Uint8(uint8_t u8, async_primitive_uint8_callback callback, void* cookie) const {
        ops_->uint8(ctx_, u8, callback, cookie);
    }

    void Uint16(uint16_t u16, async_primitive_uint16_callback callback, void* cookie) const {
        ops_->uint16(ctx_, u16, callback, cookie);
    }

    void Uint32(uint32_t u32, async_primitive_uint32_callback callback, void* cookie) const {
        ops_->uint32(ctx_, u32, callback, cookie);
    }

    void Uint64(uint64_t u64, async_primitive_uint64_callback callback, void* cookie) const {
        ops_->uint64(ctx_, u64, callback, cookie);
    }

    void Float32(float f32, async_primitive_float32_callback callback, void* cookie) const {
        ops_->float32(ctx_, f32, callback, cookie);
    }

    void Float64(double u64, async_primitive_float64_callback callback, void* cookie) const {
        ops_->float64(ctx_, u64, callback, cookie);
    }

private:
    const async_primitive_protocol_ops_t* ops_;
    void* ctx_;
};

} // namespace ddk
