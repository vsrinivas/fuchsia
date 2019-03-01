// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// WARNING: THIS FILE IS MACHINE GENERATED. DO NOT EDIT.
// Generated from the banjo.examples.protocol.primative banjo file

#pragma once

#include <banjo/examples/protocol/primative.h>
#include <ddk/driver.h>
#include <ddktl/device-internal.h>
#include <zircon/assert.h>
#include <zircon/compiler.h>
#include <zircon/types.h>

#include "primative-internal.h"

// DDK primative-protocol support
//
// :: Proxies ::
//
// ddk::SynchronousPrimativeProtocolClient is a simple wrapper around
// synchronous_primative_protocol_t. It does not own the pointers passed to it.
//
// :: Mixins ::
//
// ddk::SynchronousPrimativeProtocol is a mixin class that simplifies writing DDK drivers
// that implement the synchronous-primative protocol. It doesn't set the base protocol.
//
// :: Examples ::
//
// // A driver that implements a ZX_PROTOCOL_SYNCHRONOUS_PRIMATIVE device.
// class SynchronousPrimativeDevice;
// using SynchronousPrimativeDeviceType = ddk::Device<SynchronousPrimativeDevice, /* ddk mixins */>;
//
// class SynchronousPrimativeDevice : public SynchronousPrimativeDeviceType,
//                      public ddk::SynchronousPrimativeProtocol<SynchronousPrimativeDevice> {
//   public:
//     SynchronousPrimativeDevice(zx_device_t* parent)
//         : SynchronousPrimativeDeviceType(parent) {}
//
//     bool SynchronousPrimativeBool(bool b, bool* out_b_2);
//
//     int8_t SynchronousPrimativeInt8(int8_t i8, int8_t* out_i8_2);
//
//     int16_t SynchronousPrimativeInt16(int16_t i16, int16_t* out_i16_2);
//
//     int32_t SynchronousPrimativeInt32(int32_t i32, int32_t* out_i32_2);
//
//     int64_t SynchronousPrimativeInt64(int64_t i64, int64_t* out_i64_2);
//
//     uint8_t SynchronousPrimativeUint8(uint8_t u8, uint8_t* out_u8_2);
//
//     uint16_t SynchronousPrimativeUint16(uint16_t u16, uint16_t* out_u16_2);
//
//     uint32_t SynchronousPrimativeUint32(uint32_t u32, uint32_t* out_u32_2);
//
//     uint64_t SynchronousPrimativeUint64(uint64_t u64, uint64_t* out_u64_2);
//
//     float SynchronousPrimativeFloat32(float f32, float* out_f32_2);
//
//     double SynchronousPrimativeFloat64(double u64, double* out_f64_2);
//
//     ...
// };
// :: Proxies ::
//
// ddk::AsyncPrimativeProtocolClient is a simple wrapper around
// async_primative_protocol_t. It does not own the pointers passed to it.
//
// :: Mixins ::
//
// ddk::AsyncPrimativeProtocol is a mixin class that simplifies writing DDK drivers
// that implement the async-primative protocol. It doesn't set the base protocol.
//
// :: Examples ::
//
// // A driver that implements a ZX_PROTOCOL_ASYNC_PRIMATIVE device.
// class AsyncPrimativeDevice;
// using AsyncPrimativeDeviceType = ddk::Device<AsyncPrimativeDevice, /* ddk mixins */>;
//
// class AsyncPrimativeDevice : public AsyncPrimativeDeviceType,
//                      public ddk::AsyncPrimativeProtocol<AsyncPrimativeDevice> {
//   public:
//     AsyncPrimativeDevice(zx_device_t* parent)
//         : AsyncPrimativeDeviceType(parent) {}
//
//     void AsyncPrimativeBool(bool b, async_primative_bool_callback callback, void* cookie);
//
//     void AsyncPrimativeInt8(int8_t i8, async_primative_int8_callback callback, void* cookie);
//
//     void AsyncPrimativeInt16(int16_t i16, async_primative_int16_callback callback, void* cookie);
//
//     void AsyncPrimativeInt32(int32_t i32, async_primative_int32_callback callback, void* cookie);
//
//     void AsyncPrimativeInt64(int64_t i64, async_primative_int64_callback callback, void* cookie);
//
//     void AsyncPrimativeUint8(uint8_t u8, async_primative_uint8_callback callback, void* cookie);
//
//     void AsyncPrimativeUint16(uint16_t u16, async_primative_uint16_callback callback, void* cookie);
//
//     void AsyncPrimativeUint32(uint32_t u32, async_primative_uint32_callback callback, void* cookie);
//
//     void AsyncPrimativeUint64(uint64_t u64, async_primative_uint64_callback callback, void* cookie);
//
//     void AsyncPrimativeFloat32(float f32, async_primative_float32_callback callback, void* cookie);
//
//     void AsyncPrimativeFloat64(double u64, async_primative_float64_callback callback, void* cookie);
//
//     ...
// };

namespace ddk {

template <typename D, typename Base = internal::base_mixin>
class SynchronousPrimativeProtocol : public Base {
public:
    SynchronousPrimativeProtocol() {
        internal::CheckSynchronousPrimativeProtocolSubclass<D>();
        synchronous_primative_protocol_ops_.bool = SynchronousPrimativeBool;
        synchronous_primative_protocol_ops_.int8 = SynchronousPrimativeInt8;
        synchronous_primative_protocol_ops_.int16 = SynchronousPrimativeInt16;
        synchronous_primative_protocol_ops_.int32 = SynchronousPrimativeInt32;
        synchronous_primative_protocol_ops_.int64 = SynchronousPrimativeInt64;
        synchronous_primative_protocol_ops_.uint8 = SynchronousPrimativeUint8;
        synchronous_primative_protocol_ops_.uint16 = SynchronousPrimativeUint16;
        synchronous_primative_protocol_ops_.uint32 = SynchronousPrimativeUint32;
        synchronous_primative_protocol_ops_.uint64 = SynchronousPrimativeUint64;
        synchronous_primative_protocol_ops_.float32 = SynchronousPrimativeFloat32;
        synchronous_primative_protocol_ops_.float64 = SynchronousPrimativeFloat64;

        if constexpr (internal::is_base_proto<Base>::value) {
            auto dev = static_cast<D*>(this);
            // Can only inherit from one base_protocol implementation.
            ZX_ASSERT(dev->ddk_proto_id_ == 0);
            dev->ddk_proto_id_ = ZX_PROTOCOL_SYNCHRONOUS_PRIMATIVE;
            dev->ddk_proto_ops_ = &synchronous_primative_protocol_ops_;
        }
    }

protected:
    synchronous_primative_protocol_ops_t synchronous_primative_protocol_ops_ = {};

private:
    static bool SynchronousPrimativeBool(void* ctx, bool b, bool* out_b_2) {
        auto ret = static_cast<D*>(ctx)->SynchronousPrimativeBool(b, out_b_2);
        return ret;
    }
    static int8_t SynchronousPrimativeInt8(void* ctx, int8_t i8, int8_t* out_i8_2) {
        auto ret = static_cast<D*>(ctx)->SynchronousPrimativeInt8(i8, out_i8_2);
        return ret;
    }
    static int16_t SynchronousPrimativeInt16(void* ctx, int16_t i16, int16_t* out_i16_2) {
        auto ret = static_cast<D*>(ctx)->SynchronousPrimativeInt16(i16, out_i16_2);
        return ret;
    }
    static int32_t SynchronousPrimativeInt32(void* ctx, int32_t i32, int32_t* out_i32_2) {
        auto ret = static_cast<D*>(ctx)->SynchronousPrimativeInt32(i32, out_i32_2);
        return ret;
    }
    static int64_t SynchronousPrimativeInt64(void* ctx, int64_t i64, int64_t* out_i64_2) {
        auto ret = static_cast<D*>(ctx)->SynchronousPrimativeInt64(i64, out_i64_2);
        return ret;
    }
    static uint8_t SynchronousPrimativeUint8(void* ctx, uint8_t u8, uint8_t* out_u8_2) {
        auto ret = static_cast<D*>(ctx)->SynchronousPrimativeUint8(u8, out_u8_2);
        return ret;
    }
    static uint16_t SynchronousPrimativeUint16(void* ctx, uint16_t u16, uint16_t* out_u16_2) {
        auto ret = static_cast<D*>(ctx)->SynchronousPrimativeUint16(u16, out_u16_2);
        return ret;
    }
    static uint32_t SynchronousPrimativeUint32(void* ctx, uint32_t u32, uint32_t* out_u32_2) {
        auto ret = static_cast<D*>(ctx)->SynchronousPrimativeUint32(u32, out_u32_2);
        return ret;
    }
    static uint64_t SynchronousPrimativeUint64(void* ctx, uint64_t u64, uint64_t* out_u64_2) {
        auto ret = static_cast<D*>(ctx)->SynchronousPrimativeUint64(u64, out_u64_2);
        return ret;
    }
    static float SynchronousPrimativeFloat32(void* ctx, float f32, float* out_f32_2) {
        auto ret = static_cast<D*>(ctx)->SynchronousPrimativeFloat32(f32, out_f32_2);
        return ret;
    }
    static double SynchronousPrimativeFloat64(void* ctx, double u64, double* out_f64_2) {
        auto ret = static_cast<D*>(ctx)->SynchronousPrimativeFloat64(u64, out_f64_2);
        return ret;
    }
};

class SynchronousPrimativeProtocolClient {
public:
    SynchronousPrimativeProtocolClient()
        : ops_(nullptr), ctx_(nullptr) {}
    SynchronousPrimativeProtocolClient(const synchronous_primative_protocol_t* proto)
        : ops_(proto->ops), ctx_(proto->ctx) {}

    SynchronousPrimativeProtocolClient(zx_device_t* parent) {
        synchronous_primative_protocol_t proto;
        if (device_get_protocol(parent, ZX_PROTOCOL_SYNCHRONOUS_PRIMATIVE, &proto) == ZX_OK) {
            ops_ = proto.ops;
            ctx_ = proto.ctx;
        } else {
            ops_ = nullptr;
            ctx_ = nullptr;
        }
    }

    void GetProto(synchronous_primative_protocol_t* proto) const {
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
    synchronous_primative_protocol_ops_t* ops_;
    void* ctx_;
};

template <typename D, typename Base = internal::base_mixin>
class AsyncPrimativeProtocol : public Base {
public:
    AsyncPrimativeProtocol() {
        internal::CheckAsyncPrimativeProtocolSubclass<D>();
        async_primative_protocol_ops_.bool = AsyncPrimativeBool;
        async_primative_protocol_ops_.int8 = AsyncPrimativeInt8;
        async_primative_protocol_ops_.int16 = AsyncPrimativeInt16;
        async_primative_protocol_ops_.int32 = AsyncPrimativeInt32;
        async_primative_protocol_ops_.int64 = AsyncPrimativeInt64;
        async_primative_protocol_ops_.uint8 = AsyncPrimativeUint8;
        async_primative_protocol_ops_.uint16 = AsyncPrimativeUint16;
        async_primative_protocol_ops_.uint32 = AsyncPrimativeUint32;
        async_primative_protocol_ops_.uint64 = AsyncPrimativeUint64;
        async_primative_protocol_ops_.float32 = AsyncPrimativeFloat32;
        async_primative_protocol_ops_.float64 = AsyncPrimativeFloat64;

        if constexpr (internal::is_base_proto<Base>::value) {
            auto dev = static_cast<D*>(this);
            // Can only inherit from one base_protocol implementation.
            ZX_ASSERT(dev->ddk_proto_id_ == 0);
            dev->ddk_proto_id_ = ZX_PROTOCOL_ASYNC_PRIMATIVE;
            dev->ddk_proto_ops_ = &async_primative_protocol_ops_;
        }
    }

protected:
    async_primative_protocol_ops_t async_primative_protocol_ops_ = {};

private:
    static void AsyncPrimativeBool(void* ctx, bool b, async_primative_bool_callback callback, void* cookie) {
        static_cast<D*>(ctx)->AsyncPrimativeBool(b, callback, cookie);
    }
    static void AsyncPrimativeInt8(void* ctx, int8_t i8, async_primative_int8_callback callback, void* cookie) {
        static_cast<D*>(ctx)->AsyncPrimativeInt8(i8, callback, cookie);
    }
    static void AsyncPrimativeInt16(void* ctx, int16_t i16, async_primative_int16_callback callback, void* cookie) {
        static_cast<D*>(ctx)->AsyncPrimativeInt16(i16, callback, cookie);
    }
    static void AsyncPrimativeInt32(void* ctx, int32_t i32, async_primative_int32_callback callback, void* cookie) {
        static_cast<D*>(ctx)->AsyncPrimativeInt32(i32, callback, cookie);
    }
    static void AsyncPrimativeInt64(void* ctx, int64_t i64, async_primative_int64_callback callback, void* cookie) {
        static_cast<D*>(ctx)->AsyncPrimativeInt64(i64, callback, cookie);
    }
    static void AsyncPrimativeUint8(void* ctx, uint8_t u8, async_primative_uint8_callback callback, void* cookie) {
        static_cast<D*>(ctx)->AsyncPrimativeUint8(u8, callback, cookie);
    }
    static void AsyncPrimativeUint16(void* ctx, uint16_t u16, async_primative_uint16_callback callback, void* cookie) {
        static_cast<D*>(ctx)->AsyncPrimativeUint16(u16, callback, cookie);
    }
    static void AsyncPrimativeUint32(void* ctx, uint32_t u32, async_primative_uint32_callback callback, void* cookie) {
        static_cast<D*>(ctx)->AsyncPrimativeUint32(u32, callback, cookie);
    }
    static void AsyncPrimativeUint64(void* ctx, uint64_t u64, async_primative_uint64_callback callback, void* cookie) {
        static_cast<D*>(ctx)->AsyncPrimativeUint64(u64, callback, cookie);
    }
    static void AsyncPrimativeFloat32(void* ctx, float f32, async_primative_float32_callback callback, void* cookie) {
        static_cast<D*>(ctx)->AsyncPrimativeFloat32(f32, callback, cookie);
    }
    static void AsyncPrimativeFloat64(void* ctx, double u64, async_primative_float64_callback callback, void* cookie) {
        static_cast<D*>(ctx)->AsyncPrimativeFloat64(u64, callback, cookie);
    }
};

class AsyncPrimativeProtocolClient {
public:
    AsyncPrimativeProtocolClient()
        : ops_(nullptr), ctx_(nullptr) {}
    AsyncPrimativeProtocolClient(const async_primative_protocol_t* proto)
        : ops_(proto->ops), ctx_(proto->ctx) {}

    AsyncPrimativeProtocolClient(zx_device_t* parent) {
        async_primative_protocol_t proto;
        if (device_get_protocol(parent, ZX_PROTOCOL_ASYNC_PRIMATIVE, &proto) == ZX_OK) {
            ops_ = proto.ops;
            ctx_ = proto.ctx;
        } else {
            ops_ = nullptr;
            ctx_ = nullptr;
        }
    }

    void GetProto(async_primative_protocol_t* proto) const {
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

    void Bool(bool b, async_primative_bool_callback callback, void* cookie) const {
        ops_->bool(ctx_, b, callback, cookie);
    }

    void Int8(int8_t i8, async_primative_int8_callback callback, void* cookie) const {
        ops_->int8(ctx_, i8, callback, cookie);
    }

    void Int16(int16_t i16, async_primative_int16_callback callback, void* cookie) const {
        ops_->int16(ctx_, i16, callback, cookie);
    }

    void Int32(int32_t i32, async_primative_int32_callback callback, void* cookie) const {
        ops_->int32(ctx_, i32, callback, cookie);
    }

    void Int64(int64_t i64, async_primative_int64_callback callback, void* cookie) const {
        ops_->int64(ctx_, i64, callback, cookie);
    }

    void Uint8(uint8_t u8, async_primative_uint8_callback callback, void* cookie) const {
        ops_->uint8(ctx_, u8, callback, cookie);
    }

    void Uint16(uint16_t u16, async_primative_uint16_callback callback, void* cookie) const {
        ops_->uint16(ctx_, u16, callback, cookie);
    }

    void Uint32(uint32_t u32, async_primative_uint32_callback callback, void* cookie) const {
        ops_->uint32(ctx_, u32, callback, cookie);
    }

    void Uint64(uint64_t u64, async_primative_uint64_callback callback, void* cookie) const {
        ops_->uint64(ctx_, u64, callback, cookie);
    }

    void Float32(float f32, async_primative_float32_callback callback, void* cookie) const {
        ops_->float32(ctx_, f32, callback, cookie);
    }

    void Float64(double u64, async_primative_float64_callback callback, void* cookie) const {
        ops_->float64(ctx_, u64, callback, cookie);
    }

private:
    async_primative_protocol_ops_t* ops_;
    void* ctx_;
};

} // namespace ddk
