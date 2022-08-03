// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// WARNING: THIS FILE IS MACHINE GENERATED. DO NOT EDIT.
// Generated from the banjo.examples.protocolarray banjo file

#pragma once

#include <banjo/examples/protocolarray/c/banjo.h>
#include <ddktl/device-internal.h>
#include <lib/ddk/device.h>
#include <lib/ddk/driver.h>
#include <zircon/assert.h>
#include <zircon/compiler.h>
#include <zircon/types.h>

#include "banjo-internal.h"

// DDK protocolarray-protocol support
//
// :: Proxies ::
//
// ddk::ArrayofArraysProtocolClient is a simple wrapper around
// arrayof_arrays_protocol_t. It does not own the pointers passed to it.
//
// :: Mixins ::
//
// ddk::ArrayofArraysProtocol is a mixin class that simplifies writing DDK drivers
// that implement the arrayof-arrays protocol. It doesn't set the base protocol.
//
// :: Examples ::
//
// // A driver that implements a ZX_PROTOCOL_ARRAYOF_ARRAYS device.
// class ArrayofArraysDevice;
// using ArrayofArraysDeviceType = ddk::Device<ArrayofArraysDevice, /* ddk mixins */>;
//
// class ArrayofArraysDevice : public ArrayofArraysDeviceType,
//                      public ddk::ArrayofArraysProtocol<ArrayofArraysDevice> {
//   public:
//     ArrayofArraysDevice(zx_device_t* parent)
//         : ArrayofArraysDeviceType(parent) {}
//
//     void ArrayofArraysBool(const bool b[32][4], bool out_b[32][4]);
//
//     void ArrayofArraysInt8(const int8_t i8[32][4], int8_t out_i8[32][4]);
//
//     void ArrayofArraysInt16(const int16_t i16[32][4], int16_t out_i16[32][4]);
//
//     void ArrayofArraysInt32(const int32_t i32[32][4], int32_t out_i32[32][4]);
//
//     void ArrayofArraysInt64(const int64_t i64[32][4], int64_t out_i64[32][4]);
//
//     void ArrayofArraysUint8(const uint8_t u8[32][4], uint8_t out_u8[32][4]);
//
//     void ArrayofArraysUint16(const uint16_t u16[32][4], uint16_t out_u16[32][4]);
//
//     void ArrayofArraysUint32(const uint32_t u32[32][4], uint32_t out_u32[32][4]);
//
//     void ArrayofArraysUint64(const uint64_t u64[32][4], uint64_t out_u64[32][4]);
//
//     void ArrayofArraysFloat32(const float f32[32][4], float out_f32[32][4]);
//
//     void ArrayofArraysFloat64(const double u64[32][4], double out_f64[32][4]);
//
//     void ArrayofArraysHandle(const zx::handle u64[32][4], zx::handle out_f64[32][4]);
//
//     ...
// };
// :: Proxies ::
//
// ddk::ArrayProtocolClient is a simple wrapper around
// array_protocol_t. It does not own the pointers passed to it.
//
// :: Mixins ::
//
// ddk::ArrayProtocol is a mixin class that simplifies writing DDK drivers
// that implement the array protocol. It doesn't set the base protocol.
//
// :: Examples ::
//
// // A driver that implements a ZX_PROTOCOL_ARRAY device.
// class ArrayDevice;
// using ArrayDeviceType = ddk::Device<ArrayDevice, /* ddk mixins */>;
//
// class ArrayDevice : public ArrayDeviceType,
//                      public ddk::ArrayProtocol<ArrayDevice> {
//   public:
//     ArrayDevice(zx_device_t* parent)
//         : ArrayDeviceType(parent) {}
//
//     void ArrayBool(const bool b[1], bool out_b[1]);
//
//     void ArrayInt8(const int8_t i8[1], int8_t out_i8[1]);
//
//     void ArrayInt16(const int16_t i16[1], int16_t out_i16[1]);
//
//     void ArrayInt32(const int32_t i32[1], int32_t out_i32[1]);
//
//     void ArrayInt64(const int64_t i64[1], int64_t out_i64[1]);
//
//     void ArrayUint8(const uint8_t u8[1], uint8_t out_u8[1]);
//
//     void ArrayUint16(const uint16_t u16[1], uint16_t out_u16[1]);
//
//     void ArrayUint32(const uint32_t u32[1], uint32_t out_u32[1]);
//
//     void ArrayUint64(const uint64_t u64[1], uint64_t out_u64[1]);
//
//     void ArrayFloat32(const float f32[1], float out_f32[1]);
//
//     void ArrayFloat64(const double u64[1], double out_f64[1]);
//
//     void ArrayHandle(const zx::handle u64[1], zx::handle out_f64[1]);
//
//     ...
// };
// :: Proxies ::
//
// ddk::Array2ProtocolClient is a simple wrapper around
// array2_protocol_t. It does not own the pointers passed to it.
//
// :: Mixins ::
//
// ddk::Array2Protocol is a mixin class that simplifies writing DDK drivers
// that implement the array2 protocol. It doesn't set the base protocol.
//
// :: Examples ::
//
// // A driver that implements a ZX_PROTOCOL_ARRAY2 device.
// class Array2Device;
// using Array2DeviceType = ddk::Device<Array2Device, /* ddk mixins */>;
//
// class Array2Device : public Array2DeviceType,
//                      public ddk::Array2Protocol<Array2Device> {
//   public:
//     Array2Device(zx_device_t* parent)
//         : Array2DeviceType(parent) {}
//
//     void Array2Bool(const bool b[32], bool out_b[32]);
//
//     void Array2Int8(const int8_t i8[32], int8_t out_i8[32]);
//
//     void Array2Int16(const int16_t i16[32], int16_t out_i16[32]);
//
//     void Array2Int32(const int32_t i32[32], int32_t out_i32[32]);
//
//     void Array2Int64(const int64_t i64[32], int64_t out_i64[32]);
//
//     void Array2Uint8(const uint8_t u8[32], uint8_t out_u8[32]);
//
//     void Array2Uint16(const uint16_t u16[32], uint16_t out_u16[32]);
//
//     void Array2Uint32(const uint32_t u32[32], uint32_t out_u32[32]);
//
//     void Array2Uint64(const uint64_t u64[32], uint64_t out_u64[32]);
//
//     void Array2Float32(const float f32[32], float out_f32[32]);
//
//     void Array2Float64(const double u64[32], double out_f64[32]);
//
//     void Array2Handle(const zx::handle u64[32], zx::handle out_f64[32]);
//
//     ...
// };

namespace ddk {

template <typename D, typename Base = internal::base_mixin>
class ArrayofArraysProtocol : public Base {
public:
    ArrayofArraysProtocol() {
        internal::CheckArrayofArraysProtocolSubclass<D>();
        arrayof_arrays_protocol_ops_.bool = ArrayofArraysBool;
        arrayof_arrays_protocol_ops_.int8 = ArrayofArraysInt8;
        arrayof_arrays_protocol_ops_.int16 = ArrayofArraysInt16;
        arrayof_arrays_protocol_ops_.int32 = ArrayofArraysInt32;
        arrayof_arrays_protocol_ops_.int64 = ArrayofArraysInt64;
        arrayof_arrays_protocol_ops_.uint8 = ArrayofArraysUint8;
        arrayof_arrays_protocol_ops_.uint16 = ArrayofArraysUint16;
        arrayof_arrays_protocol_ops_.uint32 = ArrayofArraysUint32;
        arrayof_arrays_protocol_ops_.uint64 = ArrayofArraysUint64;
        arrayof_arrays_protocol_ops_.float32 = ArrayofArraysFloat32;
        arrayof_arrays_protocol_ops_.float64 = ArrayofArraysFloat64;
        arrayof_arrays_protocol_ops_.handle = ArrayofArraysHandle;

        if constexpr (internal::is_base_proto<Base>::value) {
            auto dev = static_cast<D*>(this);
            // Can only inherit from one base_protocol implementation.
            ZX_ASSERT(dev->ddk_proto_id_ == 0);
            dev->ddk_proto_id_ = ZX_PROTOCOL_ARRAYOF_ARRAYS;
            dev->ddk_proto_ops_ = &arrayof_arrays_protocol_ops_;
        }
    }

protected:
    arrayof_arrays_protocol_ops_t arrayof_arrays_protocol_ops_ = {};

private:
    static void ArrayofArraysBool(void* ctx, const bool b[32][4], bool out_b[32][4]) {
        static_cast<D*>(ctx)->ArrayofArraysBool(b, out_b);
    }
    static void ArrayofArraysInt8(void* ctx, const int8_t i8[32][4], int8_t out_i8[32][4]) {
        static_cast<D*>(ctx)->ArrayofArraysInt8(i8, out_i8);
    }
    static void ArrayofArraysInt16(void* ctx, const int16_t i16[32][4], int16_t out_i16[32][4]) {
        static_cast<D*>(ctx)->ArrayofArraysInt16(i16, out_i16);
    }
    static void ArrayofArraysInt32(void* ctx, const int32_t i32[32][4], int32_t out_i32[32][4]) {
        static_cast<D*>(ctx)->ArrayofArraysInt32(i32, out_i32);
    }
    static void ArrayofArraysInt64(void* ctx, const int64_t i64[32][4], int64_t out_i64[32][4]) {
        static_cast<D*>(ctx)->ArrayofArraysInt64(i64, out_i64);
    }
    static void ArrayofArraysUint8(void* ctx, const uint8_t u8[32][4], uint8_t out_u8[32][4]) {
        static_cast<D*>(ctx)->ArrayofArraysUint8(u8, out_u8);
    }
    static void ArrayofArraysUint16(void* ctx, const uint16_t u16[32][4], uint16_t out_u16[32][4]) {
        static_cast<D*>(ctx)->ArrayofArraysUint16(u16, out_u16);
    }
    static void ArrayofArraysUint32(void* ctx, const uint32_t u32[32][4], uint32_t out_u32[32][4]) {
        static_cast<D*>(ctx)->ArrayofArraysUint32(u32, out_u32);
    }
    static void ArrayofArraysUint64(void* ctx, const uint64_t u64[32][4], uint64_t out_u64[32][4]) {
        static_cast<D*>(ctx)->ArrayofArraysUint64(u64, out_u64);
    }
    static void ArrayofArraysFloat32(void* ctx, const float f32[32][4], float out_f32[32][4]) {
        static_cast<D*>(ctx)->ArrayofArraysFloat32(f32, out_f32);
    }
    static void ArrayofArraysFloat64(void* ctx, const double u64[32][4], double out_f64[32][4]) {
        static_cast<D*>(ctx)->ArrayofArraysFloat64(u64, out_f64);
    }
    static void ArrayofArraysHandle(void* ctx, const zx_handle_t u64[32][4], zx_handle_t out_f64[32][4]) {
        static_cast<D*>(ctx)->ArrayofArraysHandle(u64, out_f64);
    }
};

class ArrayofArraysProtocolClient {
public:
    ArrayofArraysProtocolClient()
        : ops_(nullptr), ctx_(nullptr) {}
    ArrayofArraysProtocolClient(const arrayof_arrays_protocol_t* proto)
        : ops_(proto->ops), ctx_(proto->ctx) {}

    ArrayofArraysProtocolClient(zx_device_t* parent) {
        arrayof_arrays_protocol_t proto;
        if (device_get_protocol(parent, ZX_PROTOCOL_ARRAYOF_ARRAYS, &proto) == ZX_OK) {
            ops_ = proto.ops;
            ctx_ = proto.ctx;
        } else {
            ops_ = nullptr;
            ctx_ = nullptr;
        }
    }

    ArrayofArraysProtocolClient(zx_device_t* parent, const char* fragment_name) {
        arrayof_arrays_protocol_t proto;
        if (device_get_fragment_protocol(parent, fragment_name, ZX_PROTOCOL_ARRAYOF_ARRAYS, &proto) == ZX_OK) {
            ops_ = proto.ops;
            ctx_ = proto.ctx;
        } else {
            ops_ = nullptr;
            ctx_ = nullptr;
        }
    }

    // Create a ArrayofArraysProtocolClient from the given parent device + "fragment".
    //
    // If ZX_OK is returned, the created object will be initialized in |result|.
    static zx_status_t CreateFromDevice(zx_device_t* parent,
                                        ArrayofArraysProtocolClient* result) {
        arrayof_arrays_protocol_t proto;
        zx_status_t status = device_get_protocol(
                parent, ZX_PROTOCOL_ARRAYOF_ARRAYS, &proto);
        if (status != ZX_OK) {
            return status;
        }
        *result = ArrayofArraysProtocolClient(&proto);
        return ZX_OK;
    }

    // Create a ArrayofArraysProtocolClient from the given parent device.
    //
    // If ZX_OK is returned, the created object will be initialized in |result|.
    static zx_status_t CreateFromDevice(zx_device_t* parent, const char* fragment_name,
                                        ArrayofArraysProtocolClient* result) {
        arrayof_arrays_protocol_t proto;
        zx_status_t status = device_get_fragment_protocol(parent, fragment_name,
                                 ZX_PROTOCOL_ARRAYOF_ARRAYS, &proto);
        if (status != ZX_OK) {
            return status;
        }
        *result = ArrayofArraysProtocolClient(&proto);
        return ZX_OK;
    }

    void GetProto(arrayof_arrays_protocol_t* proto) const {
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

    void Bool(const bool b[32][4], bool out_b[32][4]) const {
        ops_->bool(ctx_, b, out_b);
    }

    void Int8(const int8_t i8[32][4], int8_t out_i8[32][4]) const {
        ops_->int8(ctx_, i8, out_i8);
    }

    void Int16(const int16_t i16[32][4], int16_t out_i16[32][4]) const {
        ops_->int16(ctx_, i16, out_i16);
    }

    void Int32(const int32_t i32[32][4], int32_t out_i32[32][4]) const {
        ops_->int32(ctx_, i32, out_i32);
    }

    void Int64(const int64_t i64[32][4], int64_t out_i64[32][4]) const {
        ops_->int64(ctx_, i64, out_i64);
    }

    void Uint8(const uint8_t u8[32][4], uint8_t out_u8[32][4]) const {
        ops_->uint8(ctx_, u8, out_u8);
    }

    void Uint16(const uint16_t u16[32][4], uint16_t out_u16[32][4]) const {
        ops_->uint16(ctx_, u16, out_u16);
    }

    void Uint32(const uint32_t u32[32][4], uint32_t out_u32[32][4]) const {
        ops_->uint32(ctx_, u32, out_u32);
    }

    void Uint64(const uint64_t u64[32][4], uint64_t out_u64[32][4]) const {
        ops_->uint64(ctx_, u64, out_u64);
    }

    void Float32(const float f32[32][4], float out_f32[32][4]) const {
        ops_->float32(ctx_, f32, out_f32);
    }

    void Float64(const double u64[32][4], double out_f64[32][4]) const {
        ops_->float64(ctx_, u64, out_f64);
    }

    void Handle(const zx::handle u64[32][4], zx::handle out_f64[32][4]) const {
        ops_->handle(ctx_, u64, out_f64);
    }

private:
    const arrayof_arrays_protocol_ops_t* ops_;
    void* ctx_;
};

template <typename D, typename Base = internal::base_mixin>
class ArrayProtocol : public Base {
public:
    ArrayProtocol() {
        internal::CheckArrayProtocolSubclass<D>();
        array_protocol_ops_.bool = ArrayBool;
        array_protocol_ops_.int8 = ArrayInt8;
        array_protocol_ops_.int16 = ArrayInt16;
        array_protocol_ops_.int32 = ArrayInt32;
        array_protocol_ops_.int64 = ArrayInt64;
        array_protocol_ops_.uint8 = ArrayUint8;
        array_protocol_ops_.uint16 = ArrayUint16;
        array_protocol_ops_.uint32 = ArrayUint32;
        array_protocol_ops_.uint64 = ArrayUint64;
        array_protocol_ops_.float32 = ArrayFloat32;
        array_protocol_ops_.float64 = ArrayFloat64;
        array_protocol_ops_.handle = ArrayHandle;

        if constexpr (internal::is_base_proto<Base>::value) {
            auto dev = static_cast<D*>(this);
            // Can only inherit from one base_protocol implementation.
            ZX_ASSERT(dev->ddk_proto_id_ == 0);
            dev->ddk_proto_id_ = ZX_PROTOCOL_ARRAY;
            dev->ddk_proto_ops_ = &array_protocol_ops_;
        }
    }

protected:
    array_protocol_ops_t array_protocol_ops_ = {};

private:
    static void ArrayBool(void* ctx, const bool b[1], bool out_b[1]) {
        static_cast<D*>(ctx)->ArrayBool(b, out_b);
    }
    static void ArrayInt8(void* ctx, const int8_t i8[1], int8_t out_i8[1]) {
        static_cast<D*>(ctx)->ArrayInt8(i8, out_i8);
    }
    static void ArrayInt16(void* ctx, const int16_t i16[1], int16_t out_i16[1]) {
        static_cast<D*>(ctx)->ArrayInt16(i16, out_i16);
    }
    static void ArrayInt32(void* ctx, const int32_t i32[1], int32_t out_i32[1]) {
        static_cast<D*>(ctx)->ArrayInt32(i32, out_i32);
    }
    static void ArrayInt64(void* ctx, const int64_t i64[1], int64_t out_i64[1]) {
        static_cast<D*>(ctx)->ArrayInt64(i64, out_i64);
    }
    static void ArrayUint8(void* ctx, const uint8_t u8[1], uint8_t out_u8[1]) {
        static_cast<D*>(ctx)->ArrayUint8(u8, out_u8);
    }
    static void ArrayUint16(void* ctx, const uint16_t u16[1], uint16_t out_u16[1]) {
        static_cast<D*>(ctx)->ArrayUint16(u16, out_u16);
    }
    static void ArrayUint32(void* ctx, const uint32_t u32[1], uint32_t out_u32[1]) {
        static_cast<D*>(ctx)->ArrayUint32(u32, out_u32);
    }
    static void ArrayUint64(void* ctx, const uint64_t u64[1], uint64_t out_u64[1]) {
        static_cast<D*>(ctx)->ArrayUint64(u64, out_u64);
    }
    static void ArrayFloat32(void* ctx, const float f32[1], float out_f32[1]) {
        static_cast<D*>(ctx)->ArrayFloat32(f32, out_f32);
    }
    static void ArrayFloat64(void* ctx, const double u64[1], double out_f64[1]) {
        static_cast<D*>(ctx)->ArrayFloat64(u64, out_f64);
    }
    static void ArrayHandle(void* ctx, const zx_handle_t u64[1], zx_handle_t out_f64[1]) {
        static_cast<D*>(ctx)->ArrayHandle(u64, out_f64);
    }
};

class ArrayProtocolClient {
public:
    ArrayProtocolClient()
        : ops_(nullptr), ctx_(nullptr) {}
    ArrayProtocolClient(const array_protocol_t* proto)
        : ops_(proto->ops), ctx_(proto->ctx) {}

    ArrayProtocolClient(zx_device_t* parent) {
        array_protocol_t proto;
        if (device_get_protocol(parent, ZX_PROTOCOL_ARRAY, &proto) == ZX_OK) {
            ops_ = proto.ops;
            ctx_ = proto.ctx;
        } else {
            ops_ = nullptr;
            ctx_ = nullptr;
        }
    }

    ArrayProtocolClient(zx_device_t* parent, const char* fragment_name) {
        array_protocol_t proto;
        if (device_get_fragment_protocol(parent, fragment_name, ZX_PROTOCOL_ARRAY, &proto) == ZX_OK) {
            ops_ = proto.ops;
            ctx_ = proto.ctx;
        } else {
            ops_ = nullptr;
            ctx_ = nullptr;
        }
    }

    // Create a ArrayProtocolClient from the given parent device + "fragment".
    //
    // If ZX_OK is returned, the created object will be initialized in |result|.
    static zx_status_t CreateFromDevice(zx_device_t* parent,
                                        ArrayProtocolClient* result) {
        array_protocol_t proto;
        zx_status_t status = device_get_protocol(
                parent, ZX_PROTOCOL_ARRAY, &proto);
        if (status != ZX_OK) {
            return status;
        }
        *result = ArrayProtocolClient(&proto);
        return ZX_OK;
    }

    // Create a ArrayProtocolClient from the given parent device.
    //
    // If ZX_OK is returned, the created object will be initialized in |result|.
    static zx_status_t CreateFromDevice(zx_device_t* parent, const char* fragment_name,
                                        ArrayProtocolClient* result) {
        array_protocol_t proto;
        zx_status_t status = device_get_fragment_protocol(parent, fragment_name,
                                 ZX_PROTOCOL_ARRAY, &proto);
        if (status != ZX_OK) {
            return status;
        }
        *result = ArrayProtocolClient(&proto);
        return ZX_OK;
    }

    void GetProto(array_protocol_t* proto) const {
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

    void Bool(const bool b[1], bool out_b[1]) const {
        ops_->bool(ctx_, b, out_b);
    }

    void Int8(const int8_t i8[1], int8_t out_i8[1]) const {
        ops_->int8(ctx_, i8, out_i8);
    }

    void Int16(const int16_t i16[1], int16_t out_i16[1]) const {
        ops_->int16(ctx_, i16, out_i16);
    }

    void Int32(const int32_t i32[1], int32_t out_i32[1]) const {
        ops_->int32(ctx_, i32, out_i32);
    }

    void Int64(const int64_t i64[1], int64_t out_i64[1]) const {
        ops_->int64(ctx_, i64, out_i64);
    }

    void Uint8(const uint8_t u8[1], uint8_t out_u8[1]) const {
        ops_->uint8(ctx_, u8, out_u8);
    }

    void Uint16(const uint16_t u16[1], uint16_t out_u16[1]) const {
        ops_->uint16(ctx_, u16, out_u16);
    }

    void Uint32(const uint32_t u32[1], uint32_t out_u32[1]) const {
        ops_->uint32(ctx_, u32, out_u32);
    }

    void Uint64(const uint64_t u64[1], uint64_t out_u64[1]) const {
        ops_->uint64(ctx_, u64, out_u64);
    }

    void Float32(const float f32[1], float out_f32[1]) const {
        ops_->float32(ctx_, f32, out_f32);
    }

    void Float64(const double u64[1], double out_f64[1]) const {
        ops_->float64(ctx_, u64, out_f64);
    }

    void Handle(const zx::handle u64[1], zx::handle out_f64[1]) const {
        ops_->handle(ctx_, u64, out_f64);
    }

private:
    const array_protocol_ops_t* ops_;
    void* ctx_;
};

template <typename D, typename Base = internal::base_mixin>
class Array2Protocol : public Base {
public:
    Array2Protocol() {
        internal::CheckArray2ProtocolSubclass<D>();
        array2_protocol_ops_.bool = Array2Bool;
        array2_protocol_ops_.int8 = Array2Int8;
        array2_protocol_ops_.int16 = Array2Int16;
        array2_protocol_ops_.int32 = Array2Int32;
        array2_protocol_ops_.int64 = Array2Int64;
        array2_protocol_ops_.uint8 = Array2Uint8;
        array2_protocol_ops_.uint16 = Array2Uint16;
        array2_protocol_ops_.uint32 = Array2Uint32;
        array2_protocol_ops_.uint64 = Array2Uint64;
        array2_protocol_ops_.float32 = Array2Float32;
        array2_protocol_ops_.float64 = Array2Float64;
        array2_protocol_ops_.handle = Array2Handle;

        if constexpr (internal::is_base_proto<Base>::value) {
            auto dev = static_cast<D*>(this);
            // Can only inherit from one base_protocol implementation.
            ZX_ASSERT(dev->ddk_proto_id_ == 0);
            dev->ddk_proto_id_ = ZX_PROTOCOL_ARRAY2;
            dev->ddk_proto_ops_ = &array2_protocol_ops_;
        }
    }

protected:
    array2_protocol_ops_t array2_protocol_ops_ = {};

private:
    static void Array2Bool(void* ctx, const bool b[32], bool out_b[32]) {
        static_cast<D*>(ctx)->Array2Bool(b, out_b);
    }
    static void Array2Int8(void* ctx, const int8_t i8[32], int8_t out_i8[32]) {
        static_cast<D*>(ctx)->Array2Int8(i8, out_i8);
    }
    static void Array2Int16(void* ctx, const int16_t i16[32], int16_t out_i16[32]) {
        static_cast<D*>(ctx)->Array2Int16(i16, out_i16);
    }
    static void Array2Int32(void* ctx, const int32_t i32[32], int32_t out_i32[32]) {
        static_cast<D*>(ctx)->Array2Int32(i32, out_i32);
    }
    static void Array2Int64(void* ctx, const int64_t i64[32], int64_t out_i64[32]) {
        static_cast<D*>(ctx)->Array2Int64(i64, out_i64);
    }
    static void Array2Uint8(void* ctx, const uint8_t u8[32], uint8_t out_u8[32]) {
        static_cast<D*>(ctx)->Array2Uint8(u8, out_u8);
    }
    static void Array2Uint16(void* ctx, const uint16_t u16[32], uint16_t out_u16[32]) {
        static_cast<D*>(ctx)->Array2Uint16(u16, out_u16);
    }
    static void Array2Uint32(void* ctx, const uint32_t u32[32], uint32_t out_u32[32]) {
        static_cast<D*>(ctx)->Array2Uint32(u32, out_u32);
    }
    static void Array2Uint64(void* ctx, const uint64_t u64[32], uint64_t out_u64[32]) {
        static_cast<D*>(ctx)->Array2Uint64(u64, out_u64);
    }
    static void Array2Float32(void* ctx, const float f32[32], float out_f32[32]) {
        static_cast<D*>(ctx)->Array2Float32(f32, out_f32);
    }
    static void Array2Float64(void* ctx, const double u64[32], double out_f64[32]) {
        static_cast<D*>(ctx)->Array2Float64(u64, out_f64);
    }
    static void Array2Handle(void* ctx, const zx_handle_t u64[32], zx_handle_t out_f64[32]) {
        static_cast<D*>(ctx)->Array2Handle(u64, out_f64);
    }
};

class Array2ProtocolClient {
public:
    Array2ProtocolClient()
        : ops_(nullptr), ctx_(nullptr) {}
    Array2ProtocolClient(const array2_protocol_t* proto)
        : ops_(proto->ops), ctx_(proto->ctx) {}

    Array2ProtocolClient(zx_device_t* parent) {
        array2_protocol_t proto;
        if (device_get_protocol(parent, ZX_PROTOCOL_ARRAY2, &proto) == ZX_OK) {
            ops_ = proto.ops;
            ctx_ = proto.ctx;
        } else {
            ops_ = nullptr;
            ctx_ = nullptr;
        }
    }

    Array2ProtocolClient(zx_device_t* parent, const char* fragment_name) {
        array2_protocol_t proto;
        if (device_get_fragment_protocol(parent, fragment_name, ZX_PROTOCOL_ARRAY2, &proto) == ZX_OK) {
            ops_ = proto.ops;
            ctx_ = proto.ctx;
        } else {
            ops_ = nullptr;
            ctx_ = nullptr;
        }
    }

    // Create a Array2ProtocolClient from the given parent device + "fragment".
    //
    // If ZX_OK is returned, the created object will be initialized in |result|.
    static zx_status_t CreateFromDevice(zx_device_t* parent,
                                        Array2ProtocolClient* result) {
        array2_protocol_t proto;
        zx_status_t status = device_get_protocol(
                parent, ZX_PROTOCOL_ARRAY2, &proto);
        if (status != ZX_OK) {
            return status;
        }
        *result = Array2ProtocolClient(&proto);
        return ZX_OK;
    }

    // Create a Array2ProtocolClient from the given parent device.
    //
    // If ZX_OK is returned, the created object will be initialized in |result|.
    static zx_status_t CreateFromDevice(zx_device_t* parent, const char* fragment_name,
                                        Array2ProtocolClient* result) {
        array2_protocol_t proto;
        zx_status_t status = device_get_fragment_protocol(parent, fragment_name,
                                 ZX_PROTOCOL_ARRAY2, &proto);
        if (status != ZX_OK) {
            return status;
        }
        *result = Array2ProtocolClient(&proto);
        return ZX_OK;
    }

    void GetProto(array2_protocol_t* proto) const {
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

    void Bool(const bool b[32], bool out_b[32]) const {
        ops_->bool(ctx_, b, out_b);
    }

    void Int8(const int8_t i8[32], int8_t out_i8[32]) const {
        ops_->int8(ctx_, i8, out_i8);
    }

    void Int16(const int16_t i16[32], int16_t out_i16[32]) const {
        ops_->int16(ctx_, i16, out_i16);
    }

    void Int32(const int32_t i32[32], int32_t out_i32[32]) const {
        ops_->int32(ctx_, i32, out_i32);
    }

    void Int64(const int64_t i64[32], int64_t out_i64[32]) const {
        ops_->int64(ctx_, i64, out_i64);
    }

    void Uint8(const uint8_t u8[32], uint8_t out_u8[32]) const {
        ops_->uint8(ctx_, u8, out_u8);
    }

    void Uint16(const uint16_t u16[32], uint16_t out_u16[32]) const {
        ops_->uint16(ctx_, u16, out_u16);
    }

    void Uint32(const uint32_t u32[32], uint32_t out_u32[32]) const {
        ops_->uint32(ctx_, u32, out_u32);
    }

    void Uint64(const uint64_t u64[32], uint64_t out_u64[32]) const {
        ops_->uint64(ctx_, u64, out_u64);
    }

    void Float32(const float f32[32], float out_f32[32]) const {
        ops_->float32(ctx_, f32, out_f32);
    }

    void Float64(const double u64[32], double out_f64[32]) const {
        ops_->float64(ctx_, u64, out_f64);
    }

    void Handle(const zx::handle u64[32], zx::handle out_f64[32]) const {
        ops_->handle(ctx_, u64, out_f64);
    }

private:
    const array2_protocol_ops_t* ops_;
    void* ctx_;
};

} // namespace ddk
