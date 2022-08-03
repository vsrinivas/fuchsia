// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// WARNING: THIS FILE IS MACHINE GENERATED. DO NOT EDIT.
// Generated from the banjo.examples.protocolvector banjo file

#pragma once

#include <banjo/examples/protocolvector/c/banjo.h>
#include <ddktl/device-internal.h>
#include <lib/ddk/device.h>
#include <lib/ddk/driver.h>
#include <zircon/assert.h>
#include <zircon/compiler.h>
#include <zircon/types.h>

#include "banjo-internal.h"

// DDK protocolvector-protocol support
//
// :: Proxies ::
//
// ddk::VectorOfVectorsProtocolClient is a simple wrapper around
// vector_of_vectors_protocol_t. It does not own the pointers passed to it.
//
// :: Mixins ::
//
// ddk::VectorOfVectorsProtocol is a mixin class that simplifies writing DDK drivers
// that implement the vector-of-vectors protocol. It doesn't set the base protocol.
//
// :: Examples ::
//
// // A driver that implements a ZX_PROTOCOL_VECTOR_OF_VECTORS device.
// class VectorOfVectorsDevice;
// using VectorOfVectorsDeviceType = ddk::Device<VectorOfVectorsDevice, /* ddk mixins */>;
//
// class VectorOfVectorsDevice : public VectorOfVectorsDeviceType,
//                      public ddk::VectorOfVectorsProtocol<VectorOfVectorsDevice> {
//   public:
//     VectorOfVectorsDevice(zx_device_t* parent)
//         : VectorOfVectorsDeviceType(parent) {}
//
//     void VectorOfVectorsBool(const bool* b_list, size_t b_count, bool* out_b_list, size_t b_count, size_t* out_b_actual);
//
//     void VectorOfVectorsInt8(const int8_t* i8_list, size_t i8_count, int8_t* out_i8_list, size_t i8_count, size_t* out_i8_actual);
//
//     void VectorOfVectorsInt16(const int16_t* i16_list, size_t i16_count, int16_t* out_i16_list, size_t i16_count, size_t* out_i16_actual);
//
//     void VectorOfVectorsInt32(const int32_t* i32_list, size_t i32_count, int32_t* out_i32_list, size_t i32_count, size_t* out_i32_actual);
//
//     void VectorOfVectorsInt64(const int64_t* i64_list, size_t i64_count, int64_t* out_i64_list, size_t i64_count, size_t* out_i64_actual);
//
//     void VectorOfVectorsUint8(const uint8_t* u8_list, size_t u8_count, uint8_t* out_u8_list, size_t u8_count, size_t* out_u8_actual);
//
//     void VectorOfVectorsUint16(const uint16_t* u16_list, size_t u16_count, uint16_t* out_u16_list, size_t u16_count, size_t* out_u16_actual);
//
//     void VectorOfVectorsUint32(const uint32_t* u32_list, size_t u32_count, uint32_t* out_u32_list, size_t u32_count, size_t* out_u32_actual);
//
//     void VectorOfVectorsUint64(const uint64_t* u64_list, size_t u64_count, uint64_t* out_u64_list, size_t u64_count, size_t* out_u64_actual);
//
//     void VectorOfVectorsFloat32(const float* f32_list, size_t f32_count, float* out_f32_list, size_t f32_count, size_t* out_f32_actual);
//
//     void VectorOfVectorsFloat64(const double* u64_list, size_t u64_count, double* out_f64_list, size_t f64_count, size_t* out_f64_actual);
//
//     void VectorOfVectorsHandle(const zx_handle_t* u64_list, size_t u64_count, zx_handle_t* out_f64_list, size_t f64_count, size_t* out_f64_actual);
//
//     ...
// };
// :: Proxies ::
//
// ddk::VectorProtocolClient is a simple wrapper around
// vector_protocol_t. It does not own the pointers passed to it.
//
// :: Mixins ::
//
// ddk::VectorProtocol is a mixin class that simplifies writing DDK drivers
// that implement the vector protocol. It doesn't set the base protocol.
//
// :: Examples ::
//
// // A driver that implements a ZX_PROTOCOL_VECTOR device.
// class VectorDevice;
// using VectorDeviceType = ddk::Device<VectorDevice, /* ddk mixins */>;
//
// class VectorDevice : public VectorDeviceType,
//                      public ddk::VectorProtocol<VectorDevice> {
//   public:
//     VectorDevice(zx_device_t* parent)
//         : VectorDeviceType(parent) {}
//
//     void VectorBool(const bool* b_list, size_t b_count, bool* out_b_list, size_t b_count, size_t* out_b_actual);
//
//     void VectorInt8(const int8_t* i8_list, size_t i8_count, int8_t* out_i8_list, size_t i8_count, size_t* out_i8_actual);
//
//     void VectorInt16(const int16_t* i16_list, size_t i16_count, int16_t* out_i16_list, size_t i16_count, size_t* out_i16_actual);
//
//     void VectorInt32(const int32_t* i32_list, size_t i32_count, int32_t* out_i32_list, size_t i32_count, size_t* out_i32_actual);
//
//     void VectorInt64(const int64_t* i64_list, size_t i64_count, int64_t* out_i64_list, size_t i64_count, size_t* out_i64_actual);
//
//     void VectorUint8(const uint8_t* u8_list, size_t u8_count, uint8_t* out_u8_list, size_t u8_count, size_t* out_u8_actual);
//
//     void VectorUint16(const uint16_t* u16_list, size_t u16_count, uint16_t* out_u16_list, size_t u16_count, size_t* out_u16_actual);
//
//     void VectorUint32(const uint32_t* u32_list, size_t u32_count, uint32_t* out_u32_list, size_t u32_count, size_t* out_u32_actual);
//
//     void VectorUint64(const uint64_t* u64_list, size_t u64_count, uint64_t* out_u64_list, size_t u64_count, size_t* out_u64_actual);
//
//     void VectorFloat32(const float* f32_list, size_t f32_count, float* out_f32_list, size_t f32_count, size_t* out_f32_actual);
//
//     void VectorFloat64(const double* u64_list, size_t u64_count, double* out_f64_list, size_t f64_count, size_t* out_f64_actual);
//
//     void VectorHandle(const zx_handle_t* u64_list, size_t u64_count, zx_handle_t* out_f64_list, size_t f64_count, size_t* out_f64_actual);
//
//     ...
// };
// :: Proxies ::
//
// ddk::Vector2ProtocolClient is a simple wrapper around
// vector2_protocol_t. It does not own the pointers passed to it.
//
// :: Mixins ::
//
// ddk::Vector2Protocol is a mixin class that simplifies writing DDK drivers
// that implement the vector2 protocol. It doesn't set the base protocol.
//
// :: Examples ::
//
// // A driver that implements a ZX_PROTOCOL_VECTOR2 device.
// class Vector2Device;
// using Vector2DeviceType = ddk::Device<Vector2Device, /* ddk mixins */>;
//
// class Vector2Device : public Vector2DeviceType,
//                      public ddk::Vector2Protocol<Vector2Device> {
//   public:
//     Vector2Device(zx_device_t* parent)
//         : Vector2DeviceType(parent) {}
//
//     void Vector2Bool(const bool* b_list, size_t b_count, bool* out_b_list, size_t b_count, size_t* out_b_actual);
//
//     void Vector2Int8(const int8_t* i8_list, size_t i8_count, int8_t* out_i8_list, size_t i8_count, size_t* out_i8_actual);
//
//     void Vector2Int16(const int16_t* i16_list, size_t i16_count, int16_t* out_i16_list, size_t i16_count, size_t* out_i16_actual);
//
//     void Vector2Int32(const int32_t* i32_list, size_t i32_count, int32_t* out_i32_list, size_t i32_count, size_t* out_i32_actual);
//
//     void Vector2Int64(const int64_t* i64_list, size_t i64_count, int64_t* out_i64_list, size_t i64_count, size_t* out_i64_actual);
//
//     void Vector2Uint8(const uint8_t* u8_list, size_t u8_count, uint8_t* out_u8_list, size_t u8_count, size_t* out_u8_actual);
//
//     void Vector2Uint16(const uint16_t* u16_list, size_t u16_count, uint16_t* out_u16_list, size_t u16_count, size_t* out_u16_actual);
//
//     void Vector2Uint32(const uint32_t* u32_list, size_t u32_count, uint32_t* out_u32_list, size_t u32_count, size_t* out_u32_actual);
//
//     void Vector2Uint64(const uint64_t* u64_list, size_t u64_count, uint64_t* out_u64_list, size_t u64_count, size_t* out_u64_actual);
//
//     void Vector2Float32(const float* f32_list, size_t f32_count, float* out_f32_list, size_t f32_count, size_t* out_f32_actual);
//
//     void Vector2Float64(const double* u64_list, size_t u64_count, double* out_f64_list, size_t f64_count, size_t* out_f64_actual);
//
//     void Vector2Handle(const zx_handle_t* u64_list, size_t u64_count, zx_handle_t* out_f64_list, size_t f64_count, size_t* out_f64_actual);
//
//     ...
// };

namespace ddk {

template <typename D, typename Base = internal::base_mixin>
class VectorOfVectorsProtocol : public Base {
public:
    VectorOfVectorsProtocol() {
        internal::CheckVectorOfVectorsProtocolSubclass<D>();
        vector_of_vectors_protocol_ops_.bool = VectorOfVectorsBool;
        vector_of_vectors_protocol_ops_.int8 = VectorOfVectorsInt8;
        vector_of_vectors_protocol_ops_.int16 = VectorOfVectorsInt16;
        vector_of_vectors_protocol_ops_.int32 = VectorOfVectorsInt32;
        vector_of_vectors_protocol_ops_.int64 = VectorOfVectorsInt64;
        vector_of_vectors_protocol_ops_.uint8 = VectorOfVectorsUint8;
        vector_of_vectors_protocol_ops_.uint16 = VectorOfVectorsUint16;
        vector_of_vectors_protocol_ops_.uint32 = VectorOfVectorsUint32;
        vector_of_vectors_protocol_ops_.uint64 = VectorOfVectorsUint64;
        vector_of_vectors_protocol_ops_.float32 = VectorOfVectorsFloat32;
        vector_of_vectors_protocol_ops_.float64 = VectorOfVectorsFloat64;
        vector_of_vectors_protocol_ops_.handle = VectorOfVectorsHandle;

        if constexpr (internal::is_base_proto<Base>::value) {
            auto dev = static_cast<D*>(this);
            // Can only inherit from one base_protocol implementation.
            ZX_ASSERT(dev->ddk_proto_id_ == 0);
            dev->ddk_proto_id_ = ZX_PROTOCOL_VECTOR_OF_VECTORS;
            dev->ddk_proto_ops_ = &vector_of_vectors_protocol_ops_;
        }
    }

protected:
    vector_of_vectors_protocol_ops_t vector_of_vectors_protocol_ops_ = {};

private:
    static void VectorOfVectorsBool(void* ctx, const bool* b_list, size_t b_count, bool* out_b_list, size_t b_count, size_t* out_b_actual) {
        static_cast<D*>(ctx)->VectorOfVectorsBool(b_list, b_count, out_b_list, b_count, out_b_actual);
    }
    static void VectorOfVectorsInt8(void* ctx, const int8_t* i8_list, size_t i8_count, int8_t* out_i8_list, size_t i8_count, size_t* out_i8_actual) {
        static_cast<D*>(ctx)->VectorOfVectorsInt8(i8_list, i8_count, out_i8_list, i8_count, out_i8_actual);
    }
    static void VectorOfVectorsInt16(void* ctx, const int16_t* i16_list, size_t i16_count, int16_t* out_i16_list, size_t i16_count, size_t* out_i16_actual) {
        static_cast<D*>(ctx)->VectorOfVectorsInt16(i16_list, i16_count, out_i16_list, i16_count, out_i16_actual);
    }
    static void VectorOfVectorsInt32(void* ctx, const int32_t* i32_list, size_t i32_count, int32_t* out_i32_list, size_t i32_count, size_t* out_i32_actual) {
        static_cast<D*>(ctx)->VectorOfVectorsInt32(i32_list, i32_count, out_i32_list, i32_count, out_i32_actual);
    }
    static void VectorOfVectorsInt64(void* ctx, const int64_t* i64_list, size_t i64_count, int64_t* out_i64_list, size_t i64_count, size_t* out_i64_actual) {
        static_cast<D*>(ctx)->VectorOfVectorsInt64(i64_list, i64_count, out_i64_list, i64_count, out_i64_actual);
    }
    static void VectorOfVectorsUint8(void* ctx, const uint8_t* u8_list, size_t u8_count, uint8_t* out_u8_list, size_t u8_count, size_t* out_u8_actual) {
        static_cast<D*>(ctx)->VectorOfVectorsUint8(u8_list, u8_count, out_u8_list, u8_count, out_u8_actual);
    }
    static void VectorOfVectorsUint16(void* ctx, const uint16_t* u16_list, size_t u16_count, uint16_t* out_u16_list, size_t u16_count, size_t* out_u16_actual) {
        static_cast<D*>(ctx)->VectorOfVectorsUint16(u16_list, u16_count, out_u16_list, u16_count, out_u16_actual);
    }
    static void VectorOfVectorsUint32(void* ctx, const uint32_t* u32_list, size_t u32_count, uint32_t* out_u32_list, size_t u32_count, size_t* out_u32_actual) {
        static_cast<D*>(ctx)->VectorOfVectorsUint32(u32_list, u32_count, out_u32_list, u32_count, out_u32_actual);
    }
    static void VectorOfVectorsUint64(void* ctx, const uint64_t* u64_list, size_t u64_count, uint64_t* out_u64_list, size_t u64_count, size_t* out_u64_actual) {
        static_cast<D*>(ctx)->VectorOfVectorsUint64(u64_list, u64_count, out_u64_list, u64_count, out_u64_actual);
    }
    static void VectorOfVectorsFloat32(void* ctx, const float* f32_list, size_t f32_count, float* out_f32_list, size_t f32_count, size_t* out_f32_actual) {
        static_cast<D*>(ctx)->VectorOfVectorsFloat32(f32_list, f32_count, out_f32_list, f32_count, out_f32_actual);
    }
    static void VectorOfVectorsFloat64(void* ctx, const double* u64_list, size_t u64_count, double* out_f64_list, size_t f64_count, size_t* out_f64_actual) {
        static_cast<D*>(ctx)->VectorOfVectorsFloat64(u64_list, u64_count, out_f64_list, f64_count, out_f64_actual);
    }
    static void VectorOfVectorsHandle(void* ctx, const zx_handle_t* u64_list, size_t u64_count, zx_handle_t* out_f64_list, size_t f64_count, size_t* out_f64_actual) {
        static_cast<D*>(ctx)->VectorOfVectorsHandle(u64_list, u64_count, out_f64_list, f64_count, out_f64_actual);
    }
};

class VectorOfVectorsProtocolClient {
public:
    VectorOfVectorsProtocolClient()
        : ops_(nullptr), ctx_(nullptr) {}
    VectorOfVectorsProtocolClient(const vector_of_vectors_protocol_t* proto)
        : ops_(proto->ops), ctx_(proto->ctx) {}

    VectorOfVectorsProtocolClient(zx_device_t* parent) {
        vector_of_vectors_protocol_t proto;
        if (device_get_protocol(parent, ZX_PROTOCOL_VECTOR_OF_VECTORS, &proto) == ZX_OK) {
            ops_ = proto.ops;
            ctx_ = proto.ctx;
        } else {
            ops_ = nullptr;
            ctx_ = nullptr;
        }
    }

    VectorOfVectorsProtocolClient(zx_device_t* parent, const char* fragment_name) {
        vector_of_vectors_protocol_t proto;
        if (device_get_fragment_protocol(parent, fragment_name, ZX_PROTOCOL_VECTOR_OF_VECTORS, &proto) == ZX_OK) {
            ops_ = proto.ops;
            ctx_ = proto.ctx;
        } else {
            ops_ = nullptr;
            ctx_ = nullptr;
        }
    }

    // Create a VectorOfVectorsProtocolClient from the given parent device + "fragment".
    //
    // If ZX_OK is returned, the created object will be initialized in |result|.
    static zx_status_t CreateFromDevice(zx_device_t* parent,
                                        VectorOfVectorsProtocolClient* result) {
        vector_of_vectors_protocol_t proto;
        zx_status_t status = device_get_protocol(
                parent, ZX_PROTOCOL_VECTOR_OF_VECTORS, &proto);
        if (status != ZX_OK) {
            return status;
        }
        *result = VectorOfVectorsProtocolClient(&proto);
        return ZX_OK;
    }

    // Create a VectorOfVectorsProtocolClient from the given parent device.
    //
    // If ZX_OK is returned, the created object will be initialized in |result|.
    static zx_status_t CreateFromDevice(zx_device_t* parent, const char* fragment_name,
                                        VectorOfVectorsProtocolClient* result) {
        vector_of_vectors_protocol_t proto;
        zx_status_t status = device_get_fragment_protocol(parent, fragment_name,
                                 ZX_PROTOCOL_VECTOR_OF_VECTORS, &proto);
        if (status != ZX_OK) {
            return status;
        }
        *result = VectorOfVectorsProtocolClient(&proto);
        return ZX_OK;
    }

    void GetProto(vector_of_vectors_protocol_t* proto) const {
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

    void Bool(const bool* b_list, size_t b_count, bool* out_b_list, size_t b_count, size_t* out_b_actual) const {
        ops_->bool(ctx_, b_list, b_count, out_b_list, b_count, out_b_actual);
    }

    void Int8(const int8_t* i8_list, size_t i8_count, int8_t* out_i8_list, size_t i8_count, size_t* out_i8_actual) const {
        ops_->int8(ctx_, i8_list, i8_count, out_i8_list, i8_count, out_i8_actual);
    }

    void Int16(const int16_t* i16_list, size_t i16_count, int16_t* out_i16_list, size_t i16_count, size_t* out_i16_actual) const {
        ops_->int16(ctx_, i16_list, i16_count, out_i16_list, i16_count, out_i16_actual);
    }

    void Int32(const int32_t* i32_list, size_t i32_count, int32_t* out_i32_list, size_t i32_count, size_t* out_i32_actual) const {
        ops_->int32(ctx_, i32_list, i32_count, out_i32_list, i32_count, out_i32_actual);
    }

    void Int64(const int64_t* i64_list, size_t i64_count, int64_t* out_i64_list, size_t i64_count, size_t* out_i64_actual) const {
        ops_->int64(ctx_, i64_list, i64_count, out_i64_list, i64_count, out_i64_actual);
    }

    void Uint8(const uint8_t* u8_list, size_t u8_count, uint8_t* out_u8_list, size_t u8_count, size_t* out_u8_actual) const {
        ops_->uint8(ctx_, u8_list, u8_count, out_u8_list, u8_count, out_u8_actual);
    }

    void Uint16(const uint16_t* u16_list, size_t u16_count, uint16_t* out_u16_list, size_t u16_count, size_t* out_u16_actual) const {
        ops_->uint16(ctx_, u16_list, u16_count, out_u16_list, u16_count, out_u16_actual);
    }

    void Uint32(const uint32_t* u32_list, size_t u32_count, uint32_t* out_u32_list, size_t u32_count, size_t* out_u32_actual) const {
        ops_->uint32(ctx_, u32_list, u32_count, out_u32_list, u32_count, out_u32_actual);
    }

    void Uint64(const uint64_t* u64_list, size_t u64_count, uint64_t* out_u64_list, size_t u64_count, size_t* out_u64_actual) const {
        ops_->uint64(ctx_, u64_list, u64_count, out_u64_list, u64_count, out_u64_actual);
    }

    void Float32(const float* f32_list, size_t f32_count, float* out_f32_list, size_t f32_count, size_t* out_f32_actual) const {
        ops_->float32(ctx_, f32_list, f32_count, out_f32_list, f32_count, out_f32_actual);
    }

    void Float64(const double* u64_list, size_t u64_count, double* out_f64_list, size_t f64_count, size_t* out_f64_actual) const {
        ops_->float64(ctx_, u64_list, u64_count, out_f64_list, f64_count, out_f64_actual);
    }

    void Handle(const zx_handle_t* u64_list, size_t u64_count, zx_handle_t* out_f64_list, size_t f64_count, size_t* out_f64_actual) const {
        ops_->handle(ctx_, u64_list, u64_count, out_f64_list, f64_count, out_f64_actual);
    }

private:
    const vector_of_vectors_protocol_ops_t* ops_;
    void* ctx_;
};

template <typename D, typename Base = internal::base_mixin>
class VectorProtocol : public Base {
public:
    VectorProtocol() {
        internal::CheckVectorProtocolSubclass<D>();
        vector_protocol_ops_.bool = VectorBool;
        vector_protocol_ops_.int8 = VectorInt8;
        vector_protocol_ops_.int16 = VectorInt16;
        vector_protocol_ops_.int32 = VectorInt32;
        vector_protocol_ops_.int64 = VectorInt64;
        vector_protocol_ops_.uint8 = VectorUint8;
        vector_protocol_ops_.uint16 = VectorUint16;
        vector_protocol_ops_.uint32 = VectorUint32;
        vector_protocol_ops_.uint64 = VectorUint64;
        vector_protocol_ops_.float32 = VectorFloat32;
        vector_protocol_ops_.float64 = VectorFloat64;
        vector_protocol_ops_.handle = VectorHandle;

        if constexpr (internal::is_base_proto<Base>::value) {
            auto dev = static_cast<D*>(this);
            // Can only inherit from one base_protocol implementation.
            ZX_ASSERT(dev->ddk_proto_id_ == 0);
            dev->ddk_proto_id_ = ZX_PROTOCOL_VECTOR;
            dev->ddk_proto_ops_ = &vector_protocol_ops_;
        }
    }

protected:
    vector_protocol_ops_t vector_protocol_ops_ = {};

private:
    static void VectorBool(void* ctx, const bool* b_list, size_t b_count, bool* out_b_list, size_t b_count, size_t* out_b_actual) {
        static_cast<D*>(ctx)->VectorBool(b_list, b_count, out_b_list, b_count, out_b_actual);
    }
    static void VectorInt8(void* ctx, const int8_t* i8_list, size_t i8_count, int8_t* out_i8_list, size_t i8_count, size_t* out_i8_actual) {
        static_cast<D*>(ctx)->VectorInt8(i8_list, i8_count, out_i8_list, i8_count, out_i8_actual);
    }
    static void VectorInt16(void* ctx, const int16_t* i16_list, size_t i16_count, int16_t* out_i16_list, size_t i16_count, size_t* out_i16_actual) {
        static_cast<D*>(ctx)->VectorInt16(i16_list, i16_count, out_i16_list, i16_count, out_i16_actual);
    }
    static void VectorInt32(void* ctx, const int32_t* i32_list, size_t i32_count, int32_t* out_i32_list, size_t i32_count, size_t* out_i32_actual) {
        static_cast<D*>(ctx)->VectorInt32(i32_list, i32_count, out_i32_list, i32_count, out_i32_actual);
    }
    static void VectorInt64(void* ctx, const int64_t* i64_list, size_t i64_count, int64_t* out_i64_list, size_t i64_count, size_t* out_i64_actual) {
        static_cast<D*>(ctx)->VectorInt64(i64_list, i64_count, out_i64_list, i64_count, out_i64_actual);
    }
    static void VectorUint8(void* ctx, const uint8_t* u8_list, size_t u8_count, uint8_t* out_u8_list, size_t u8_count, size_t* out_u8_actual) {
        static_cast<D*>(ctx)->VectorUint8(u8_list, u8_count, out_u8_list, u8_count, out_u8_actual);
    }
    static void VectorUint16(void* ctx, const uint16_t* u16_list, size_t u16_count, uint16_t* out_u16_list, size_t u16_count, size_t* out_u16_actual) {
        static_cast<D*>(ctx)->VectorUint16(u16_list, u16_count, out_u16_list, u16_count, out_u16_actual);
    }
    static void VectorUint32(void* ctx, const uint32_t* u32_list, size_t u32_count, uint32_t* out_u32_list, size_t u32_count, size_t* out_u32_actual) {
        static_cast<D*>(ctx)->VectorUint32(u32_list, u32_count, out_u32_list, u32_count, out_u32_actual);
    }
    static void VectorUint64(void* ctx, const uint64_t* u64_list, size_t u64_count, uint64_t* out_u64_list, size_t u64_count, size_t* out_u64_actual) {
        static_cast<D*>(ctx)->VectorUint64(u64_list, u64_count, out_u64_list, u64_count, out_u64_actual);
    }
    static void VectorFloat32(void* ctx, const float* f32_list, size_t f32_count, float* out_f32_list, size_t f32_count, size_t* out_f32_actual) {
        static_cast<D*>(ctx)->VectorFloat32(f32_list, f32_count, out_f32_list, f32_count, out_f32_actual);
    }
    static void VectorFloat64(void* ctx, const double* u64_list, size_t u64_count, double* out_f64_list, size_t f64_count, size_t* out_f64_actual) {
        static_cast<D*>(ctx)->VectorFloat64(u64_list, u64_count, out_f64_list, f64_count, out_f64_actual);
    }
    static void VectorHandle(void* ctx, const zx_handle_t* u64_list, size_t u64_count, zx_handle_t* out_f64_list, size_t f64_count, size_t* out_f64_actual) {
        static_cast<D*>(ctx)->VectorHandle(u64_list, u64_count, out_f64_list, f64_count, out_f64_actual);
    }
};

class VectorProtocolClient {
public:
    VectorProtocolClient()
        : ops_(nullptr), ctx_(nullptr) {}
    VectorProtocolClient(const vector_protocol_t* proto)
        : ops_(proto->ops), ctx_(proto->ctx) {}

    VectorProtocolClient(zx_device_t* parent) {
        vector_protocol_t proto;
        if (device_get_protocol(parent, ZX_PROTOCOL_VECTOR, &proto) == ZX_OK) {
            ops_ = proto.ops;
            ctx_ = proto.ctx;
        } else {
            ops_ = nullptr;
            ctx_ = nullptr;
        }
    }

    VectorProtocolClient(zx_device_t* parent, const char* fragment_name) {
        vector_protocol_t proto;
        if (device_get_fragment_protocol(parent, fragment_name, ZX_PROTOCOL_VECTOR, &proto) == ZX_OK) {
            ops_ = proto.ops;
            ctx_ = proto.ctx;
        } else {
            ops_ = nullptr;
            ctx_ = nullptr;
        }
    }

    // Create a VectorProtocolClient from the given parent device + "fragment".
    //
    // If ZX_OK is returned, the created object will be initialized in |result|.
    static zx_status_t CreateFromDevice(zx_device_t* parent,
                                        VectorProtocolClient* result) {
        vector_protocol_t proto;
        zx_status_t status = device_get_protocol(
                parent, ZX_PROTOCOL_VECTOR, &proto);
        if (status != ZX_OK) {
            return status;
        }
        *result = VectorProtocolClient(&proto);
        return ZX_OK;
    }

    // Create a VectorProtocolClient from the given parent device.
    //
    // If ZX_OK is returned, the created object will be initialized in |result|.
    static zx_status_t CreateFromDevice(zx_device_t* parent, const char* fragment_name,
                                        VectorProtocolClient* result) {
        vector_protocol_t proto;
        zx_status_t status = device_get_fragment_protocol(parent, fragment_name,
                                 ZX_PROTOCOL_VECTOR, &proto);
        if (status != ZX_OK) {
            return status;
        }
        *result = VectorProtocolClient(&proto);
        return ZX_OK;
    }

    void GetProto(vector_protocol_t* proto) const {
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

    void Bool(const bool* b_list, size_t b_count, bool* out_b_list, size_t b_count, size_t* out_b_actual) const {
        ops_->bool(ctx_, b_list, b_count, out_b_list, b_count, out_b_actual);
    }

    void Int8(const int8_t* i8_list, size_t i8_count, int8_t* out_i8_list, size_t i8_count, size_t* out_i8_actual) const {
        ops_->int8(ctx_, i8_list, i8_count, out_i8_list, i8_count, out_i8_actual);
    }

    void Int16(const int16_t* i16_list, size_t i16_count, int16_t* out_i16_list, size_t i16_count, size_t* out_i16_actual) const {
        ops_->int16(ctx_, i16_list, i16_count, out_i16_list, i16_count, out_i16_actual);
    }

    void Int32(const int32_t* i32_list, size_t i32_count, int32_t* out_i32_list, size_t i32_count, size_t* out_i32_actual) const {
        ops_->int32(ctx_, i32_list, i32_count, out_i32_list, i32_count, out_i32_actual);
    }

    void Int64(const int64_t* i64_list, size_t i64_count, int64_t* out_i64_list, size_t i64_count, size_t* out_i64_actual) const {
        ops_->int64(ctx_, i64_list, i64_count, out_i64_list, i64_count, out_i64_actual);
    }

    void Uint8(const uint8_t* u8_list, size_t u8_count, uint8_t* out_u8_list, size_t u8_count, size_t* out_u8_actual) const {
        ops_->uint8(ctx_, u8_list, u8_count, out_u8_list, u8_count, out_u8_actual);
    }

    void Uint16(const uint16_t* u16_list, size_t u16_count, uint16_t* out_u16_list, size_t u16_count, size_t* out_u16_actual) const {
        ops_->uint16(ctx_, u16_list, u16_count, out_u16_list, u16_count, out_u16_actual);
    }

    void Uint32(const uint32_t* u32_list, size_t u32_count, uint32_t* out_u32_list, size_t u32_count, size_t* out_u32_actual) const {
        ops_->uint32(ctx_, u32_list, u32_count, out_u32_list, u32_count, out_u32_actual);
    }

    void Uint64(const uint64_t* u64_list, size_t u64_count, uint64_t* out_u64_list, size_t u64_count, size_t* out_u64_actual) const {
        ops_->uint64(ctx_, u64_list, u64_count, out_u64_list, u64_count, out_u64_actual);
    }

    void Float32(const float* f32_list, size_t f32_count, float* out_f32_list, size_t f32_count, size_t* out_f32_actual) const {
        ops_->float32(ctx_, f32_list, f32_count, out_f32_list, f32_count, out_f32_actual);
    }

    void Float64(const double* u64_list, size_t u64_count, double* out_f64_list, size_t f64_count, size_t* out_f64_actual) const {
        ops_->float64(ctx_, u64_list, u64_count, out_f64_list, f64_count, out_f64_actual);
    }

    void Handle(const zx_handle_t* u64_list, size_t u64_count, zx_handle_t* out_f64_list, size_t f64_count, size_t* out_f64_actual) const {
        ops_->handle(ctx_, u64_list, u64_count, out_f64_list, f64_count, out_f64_actual);
    }

private:
    const vector_protocol_ops_t* ops_;
    void* ctx_;
};

template <typename D, typename Base = internal::base_mixin>
class Vector2Protocol : public Base {
public:
    Vector2Protocol() {
        internal::CheckVector2ProtocolSubclass<D>();
        vector2_protocol_ops_.bool = Vector2Bool;
        vector2_protocol_ops_.int8 = Vector2Int8;
        vector2_protocol_ops_.int16 = Vector2Int16;
        vector2_protocol_ops_.int32 = Vector2Int32;
        vector2_protocol_ops_.int64 = Vector2Int64;
        vector2_protocol_ops_.uint8 = Vector2Uint8;
        vector2_protocol_ops_.uint16 = Vector2Uint16;
        vector2_protocol_ops_.uint32 = Vector2Uint32;
        vector2_protocol_ops_.uint64 = Vector2Uint64;
        vector2_protocol_ops_.float32 = Vector2Float32;
        vector2_protocol_ops_.float64 = Vector2Float64;
        vector2_protocol_ops_.handle = Vector2Handle;

        if constexpr (internal::is_base_proto<Base>::value) {
            auto dev = static_cast<D*>(this);
            // Can only inherit from one base_protocol implementation.
            ZX_ASSERT(dev->ddk_proto_id_ == 0);
            dev->ddk_proto_id_ = ZX_PROTOCOL_VECTOR2;
            dev->ddk_proto_ops_ = &vector2_protocol_ops_;
        }
    }

protected:
    vector2_protocol_ops_t vector2_protocol_ops_ = {};

private:
    static void Vector2Bool(void* ctx, const bool* b_list, size_t b_count, bool* out_b_list, size_t b_count, size_t* out_b_actual) {
        static_cast<D*>(ctx)->Vector2Bool(b_list, b_count, out_b_list, b_count, out_b_actual);
    }
    static void Vector2Int8(void* ctx, const int8_t* i8_list, size_t i8_count, int8_t* out_i8_list, size_t i8_count, size_t* out_i8_actual) {
        static_cast<D*>(ctx)->Vector2Int8(i8_list, i8_count, out_i8_list, i8_count, out_i8_actual);
    }
    static void Vector2Int16(void* ctx, const int16_t* i16_list, size_t i16_count, int16_t* out_i16_list, size_t i16_count, size_t* out_i16_actual) {
        static_cast<D*>(ctx)->Vector2Int16(i16_list, i16_count, out_i16_list, i16_count, out_i16_actual);
    }
    static void Vector2Int32(void* ctx, const int32_t* i32_list, size_t i32_count, int32_t* out_i32_list, size_t i32_count, size_t* out_i32_actual) {
        static_cast<D*>(ctx)->Vector2Int32(i32_list, i32_count, out_i32_list, i32_count, out_i32_actual);
    }
    static void Vector2Int64(void* ctx, const int64_t* i64_list, size_t i64_count, int64_t* out_i64_list, size_t i64_count, size_t* out_i64_actual) {
        static_cast<D*>(ctx)->Vector2Int64(i64_list, i64_count, out_i64_list, i64_count, out_i64_actual);
    }
    static void Vector2Uint8(void* ctx, const uint8_t* u8_list, size_t u8_count, uint8_t* out_u8_list, size_t u8_count, size_t* out_u8_actual) {
        static_cast<D*>(ctx)->Vector2Uint8(u8_list, u8_count, out_u8_list, u8_count, out_u8_actual);
    }
    static void Vector2Uint16(void* ctx, const uint16_t* u16_list, size_t u16_count, uint16_t* out_u16_list, size_t u16_count, size_t* out_u16_actual) {
        static_cast<D*>(ctx)->Vector2Uint16(u16_list, u16_count, out_u16_list, u16_count, out_u16_actual);
    }
    static void Vector2Uint32(void* ctx, const uint32_t* u32_list, size_t u32_count, uint32_t* out_u32_list, size_t u32_count, size_t* out_u32_actual) {
        static_cast<D*>(ctx)->Vector2Uint32(u32_list, u32_count, out_u32_list, u32_count, out_u32_actual);
    }
    static void Vector2Uint64(void* ctx, const uint64_t* u64_list, size_t u64_count, uint64_t* out_u64_list, size_t u64_count, size_t* out_u64_actual) {
        static_cast<D*>(ctx)->Vector2Uint64(u64_list, u64_count, out_u64_list, u64_count, out_u64_actual);
    }
    static void Vector2Float32(void* ctx, const float* f32_list, size_t f32_count, float* out_f32_list, size_t f32_count, size_t* out_f32_actual) {
        static_cast<D*>(ctx)->Vector2Float32(f32_list, f32_count, out_f32_list, f32_count, out_f32_actual);
    }
    static void Vector2Float64(void* ctx, const double* u64_list, size_t u64_count, double* out_f64_list, size_t f64_count, size_t* out_f64_actual) {
        static_cast<D*>(ctx)->Vector2Float64(u64_list, u64_count, out_f64_list, f64_count, out_f64_actual);
    }
    static void Vector2Handle(void* ctx, const zx_handle_t* u64_list, size_t u64_count, zx_handle_t* out_f64_list, size_t f64_count, size_t* out_f64_actual) {
        static_cast<D*>(ctx)->Vector2Handle(u64_list, u64_count, out_f64_list, f64_count, out_f64_actual);
    }
};

class Vector2ProtocolClient {
public:
    Vector2ProtocolClient()
        : ops_(nullptr), ctx_(nullptr) {}
    Vector2ProtocolClient(const vector2_protocol_t* proto)
        : ops_(proto->ops), ctx_(proto->ctx) {}

    Vector2ProtocolClient(zx_device_t* parent) {
        vector2_protocol_t proto;
        if (device_get_protocol(parent, ZX_PROTOCOL_VECTOR2, &proto) == ZX_OK) {
            ops_ = proto.ops;
            ctx_ = proto.ctx;
        } else {
            ops_ = nullptr;
            ctx_ = nullptr;
        }
    }

    Vector2ProtocolClient(zx_device_t* parent, const char* fragment_name) {
        vector2_protocol_t proto;
        if (device_get_fragment_protocol(parent, fragment_name, ZX_PROTOCOL_VECTOR2, &proto) == ZX_OK) {
            ops_ = proto.ops;
            ctx_ = proto.ctx;
        } else {
            ops_ = nullptr;
            ctx_ = nullptr;
        }
    }

    // Create a Vector2ProtocolClient from the given parent device + "fragment".
    //
    // If ZX_OK is returned, the created object will be initialized in |result|.
    static zx_status_t CreateFromDevice(zx_device_t* parent,
                                        Vector2ProtocolClient* result) {
        vector2_protocol_t proto;
        zx_status_t status = device_get_protocol(
                parent, ZX_PROTOCOL_VECTOR2, &proto);
        if (status != ZX_OK) {
            return status;
        }
        *result = Vector2ProtocolClient(&proto);
        return ZX_OK;
    }

    // Create a Vector2ProtocolClient from the given parent device.
    //
    // If ZX_OK is returned, the created object will be initialized in |result|.
    static zx_status_t CreateFromDevice(zx_device_t* parent, const char* fragment_name,
                                        Vector2ProtocolClient* result) {
        vector2_protocol_t proto;
        zx_status_t status = device_get_fragment_protocol(parent, fragment_name,
                                 ZX_PROTOCOL_VECTOR2, &proto);
        if (status != ZX_OK) {
            return status;
        }
        *result = Vector2ProtocolClient(&proto);
        return ZX_OK;
    }

    void GetProto(vector2_protocol_t* proto) const {
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

    void Bool(const bool* b_list, size_t b_count, bool* out_b_list, size_t b_count, size_t* out_b_actual) const {
        ops_->bool(ctx_, b_list, b_count, out_b_list, b_count, out_b_actual);
    }

    void Int8(const int8_t* i8_list, size_t i8_count, int8_t* out_i8_list, size_t i8_count, size_t* out_i8_actual) const {
        ops_->int8(ctx_, i8_list, i8_count, out_i8_list, i8_count, out_i8_actual);
    }

    void Int16(const int16_t* i16_list, size_t i16_count, int16_t* out_i16_list, size_t i16_count, size_t* out_i16_actual) const {
        ops_->int16(ctx_, i16_list, i16_count, out_i16_list, i16_count, out_i16_actual);
    }

    void Int32(const int32_t* i32_list, size_t i32_count, int32_t* out_i32_list, size_t i32_count, size_t* out_i32_actual) const {
        ops_->int32(ctx_, i32_list, i32_count, out_i32_list, i32_count, out_i32_actual);
    }

    void Int64(const int64_t* i64_list, size_t i64_count, int64_t* out_i64_list, size_t i64_count, size_t* out_i64_actual) const {
        ops_->int64(ctx_, i64_list, i64_count, out_i64_list, i64_count, out_i64_actual);
    }

    void Uint8(const uint8_t* u8_list, size_t u8_count, uint8_t* out_u8_list, size_t u8_count, size_t* out_u8_actual) const {
        ops_->uint8(ctx_, u8_list, u8_count, out_u8_list, u8_count, out_u8_actual);
    }

    void Uint16(const uint16_t* u16_list, size_t u16_count, uint16_t* out_u16_list, size_t u16_count, size_t* out_u16_actual) const {
        ops_->uint16(ctx_, u16_list, u16_count, out_u16_list, u16_count, out_u16_actual);
    }

    void Uint32(const uint32_t* u32_list, size_t u32_count, uint32_t* out_u32_list, size_t u32_count, size_t* out_u32_actual) const {
        ops_->uint32(ctx_, u32_list, u32_count, out_u32_list, u32_count, out_u32_actual);
    }

    void Uint64(const uint64_t* u64_list, size_t u64_count, uint64_t* out_u64_list, size_t u64_count, size_t* out_u64_actual) const {
        ops_->uint64(ctx_, u64_list, u64_count, out_u64_list, u64_count, out_u64_actual);
    }

    void Float32(const float* f32_list, size_t f32_count, float* out_f32_list, size_t f32_count, size_t* out_f32_actual) const {
        ops_->float32(ctx_, f32_list, f32_count, out_f32_list, f32_count, out_f32_actual);
    }

    void Float64(const double* u64_list, size_t u64_count, double* out_f64_list, size_t f64_count, size_t* out_f64_actual) const {
        ops_->float64(ctx_, u64_list, u64_count, out_f64_list, f64_count, out_f64_actual);
    }

    void Handle(const zx_handle_t* u64_list, size_t u64_count, zx_handle_t* out_f64_list, size_t f64_count, size_t* out_f64_actual) const {
        ops_->handle(ctx_, u64_list, u64_count, out_f64_list, f64_count, out_f64_actual);
    }

private:
    const vector2_protocol_ops_t* ops_;
    void* ctx_;
};

} // namespace ddk
