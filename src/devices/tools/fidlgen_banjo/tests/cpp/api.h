// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// WARNING: THIS FILE IS MACHINE GENERATED. DO NOT EDIT.
// Generated from the banjo.examples.api banjo file

#pragma once

#include <banjo/examples/api/c/banjo.h>
#include <ddktl/device-internal.h>
#include <lib/ddk/device.h>
#include <lib/ddk/driver.h>
#include <lib/zx/handle.h>
#include <zircon/assert.h>
#include <zircon/compiler.h>
#include <zircon/types.h>

#include "banjo-internal.h"

// DDK api-protocol support
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
//     zx_status_t Apibool(zx::handle handle, bool data);
//
//     zx_status_t Apiint8(zx::handle handle, int8_t data);
//
//     zx_status_t Apiint16(zx::handle handle, int16_t data);
//
//     zx_status_t Apiint32(zx::handle handle, int32_t data);
//
//     zx_status_t Apiint64(zx::handle handle, int64_t data);
//
//     zx_status_t Apiuint8(zx::handle handle, uint8_t data);
//
//     zx_status_t Apiuint16(zx::handle handle, uint16_t data);
//
//     zx_status_t Apiuint32(zx::handle handle, uint32_t data);
//
//     zx_status_t Apiuint64(zx::handle handle, uint64_t data);
//
//     zx_status_t Apifloat32(zx::handle handle, float data);
//
//     zx_status_t Apifloat64(zx::handle handle, double data);
//
//     zx_status_t Apiduration(zx::handle handle, zx_duration_t data);
//
//     zx_status_t Apikoid(zx::handle handle, zx_koid_t data);
//
//     zx_status_t Apipaddr(zx::handle handle, zx_paddr_t data);
//
//     zx_status_t Apisignals(zx::handle handle, zx_signals_t data);
//
//     zx_status_t Apitime(zx::handle handle, zx_time_t data);
//
//     zx_status_t Apivaddr(zx::handle handle, zx_vaddr_t data);
//
//     zx_status_t Apioutput_bool(zx::handle handle, bool* out_result);
//
//     zx_status_t Apioutput_int8(zx::handle handle, int8_t* out_result);
//
//     zx_status_t Apioutput_int16(zx::handle handle, int16_t* out_result);
//
//     zx_status_t Apioutput_int32(zx::handle handle, int32_t* out_result);
//
//     zx_status_t Apioutput_int64(zx::handle handle, int64_t* out_result);
//
//     zx_status_t Apioutput_uint8(zx::handle handle, uint8_t* out_result);
//
//     zx_status_t Apioutput_uint16(zx::handle handle, uint16_t* out_result);
//
//     zx_status_t Apioutput_uint32(zx::handle handle, uint32_t* out_result);
//
//     zx_status_t Apioutput_uint64(zx::handle handle, uint64_t* out_result);
//
//     zx_status_t Apioutput_float32(zx::handle handle, float* out_result);
//
//     zx_status_t Apioutput_float64(zx::handle handle, double* out_result);
//
//     zx_status_t Apioutput_duration(zx::handle handle, zx_duration_t* out_result);
//
//     zx_status_t Apioutput_koid(zx::handle handle, zx_koid_t* out_result);
//
//     zx_status_t Apioutput_paddr(zx::handle handle, zx_paddr_t* out_result);
//
//     zx_status_t Apioutput_signals(zx::handle handle, zx_signals_t* out_result);
//
//     zx_status_t Apioutput_time(zx::handle handle, zx_time_t* out_result);
//
//     zx_status_t Apioutput_vaddr(zx::handle handle, zx_vaddr_t* out_result);
//
//     void Apireturn_void(zx::handle handle);
//
//     zx_status_t Apireturn_status(zx::handle handle);
//
//     zx_ticks_t Apireturn_ticks(zx::handle handle);
//
//     zx_time_t Apireturn_time(zx::handle handle);
//
//     uint32_t Apireturn_uint32(zx::handle handle);
//
//     uint64_t Apireturn_uint64(zx::handle handle);
//
//     ...
// };

namespace ddk {

template <typename D, typename Base = internal::base_mixin>
class ApiProtocol : public Base {
public:
    ApiProtocol() {
        internal::CheckApiProtocolSubclass<D>();
        api_protocol_ops_.bool = Apibool;
        api_protocol_ops_.int8 = Apiint8;
        api_protocol_ops_.int16 = Apiint16;
        api_protocol_ops_.int32 = Apiint32;
        api_protocol_ops_.int64 = Apiint64;
        api_protocol_ops_.uint8 = Apiuint8;
        api_protocol_ops_.uint16 = Apiuint16;
        api_protocol_ops_.uint32 = Apiuint32;
        api_protocol_ops_.uint64 = Apiuint64;
        api_protocol_ops_.float32 = Apifloat32;
        api_protocol_ops_.float64 = Apifloat64;
        api_protocol_ops_.duration = Apiduration;
        api_protocol_ops_.koid = Apikoid;
        api_protocol_ops_.paddr = Apipaddr;
        api_protocol_ops_.signals = Apisignals;
        api_protocol_ops_.time = Apitime;
        api_protocol_ops_.vaddr = Apivaddr;
        api_protocol_ops_.output_bool = Apioutput_bool;
        api_protocol_ops_.output_int8 = Apioutput_int8;
        api_protocol_ops_.output_int16 = Apioutput_int16;
        api_protocol_ops_.output_int32 = Apioutput_int32;
        api_protocol_ops_.output_int64 = Apioutput_int64;
        api_protocol_ops_.output_uint8 = Apioutput_uint8;
        api_protocol_ops_.output_uint16 = Apioutput_uint16;
        api_protocol_ops_.output_uint32 = Apioutput_uint32;
        api_protocol_ops_.output_uint64 = Apioutput_uint64;
        api_protocol_ops_.output_float32 = Apioutput_float32;
        api_protocol_ops_.output_float64 = Apioutput_float64;
        api_protocol_ops_.output_duration = Apioutput_duration;
        api_protocol_ops_.output_koid = Apioutput_koid;
        api_protocol_ops_.output_paddr = Apioutput_paddr;
        api_protocol_ops_.output_signals = Apioutput_signals;
        api_protocol_ops_.output_time = Apioutput_time;
        api_protocol_ops_.output_vaddr = Apioutput_vaddr;
        api_protocol_ops_.return_void = Apireturn_void;
        api_protocol_ops_.return_status = Apireturn_status;
        api_protocol_ops_.return_ticks = Apireturn_ticks;
        api_protocol_ops_.return_time = Apireturn_time;
        api_protocol_ops_.return_uint32 = Apireturn_uint32;
        api_protocol_ops_.return_uint64 = Apireturn_uint64;

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
    static zx_status_t Apibool(void* ctx, zx_handle_t handle, bool data) {
        auto ret = static_cast<D*>(ctx)->Apibool(zx::handle(handle), data);
        return ret;
    }
    static zx_status_t Apiint8(void* ctx, zx_handle_t handle, int8_t data) {
        auto ret = static_cast<D*>(ctx)->Apiint8(zx::handle(handle), data);
        return ret;
    }
    static zx_status_t Apiint16(void* ctx, zx_handle_t handle, int16_t data) {
        auto ret = static_cast<D*>(ctx)->Apiint16(zx::handle(handle), data);
        return ret;
    }
    static zx_status_t Apiint32(void* ctx, zx_handle_t handle, int32_t data) {
        auto ret = static_cast<D*>(ctx)->Apiint32(zx::handle(handle), data);
        return ret;
    }
    static zx_status_t Apiint64(void* ctx, zx_handle_t handle, int64_t data) {
        auto ret = static_cast<D*>(ctx)->Apiint64(zx::handle(handle), data);
        return ret;
    }
    static zx_status_t Apiuint8(void* ctx, zx_handle_t handle, uint8_t data) {
        auto ret = static_cast<D*>(ctx)->Apiuint8(zx::handle(handle), data);
        return ret;
    }
    static zx_status_t Apiuint16(void* ctx, zx_handle_t handle, uint16_t data) {
        auto ret = static_cast<D*>(ctx)->Apiuint16(zx::handle(handle), data);
        return ret;
    }
    static zx_status_t Apiuint32(void* ctx, zx_handle_t handle, uint32_t data) {
        auto ret = static_cast<D*>(ctx)->Apiuint32(zx::handle(handle), data);
        return ret;
    }
    static zx_status_t Apiuint64(void* ctx, zx_handle_t handle, uint64_t data) {
        auto ret = static_cast<D*>(ctx)->Apiuint64(zx::handle(handle), data);
        return ret;
    }
    static zx_status_t Apifloat32(void* ctx, zx_handle_t handle, float data) {
        auto ret = static_cast<D*>(ctx)->Apifloat32(zx::handle(handle), data);
        return ret;
    }
    static zx_status_t Apifloat64(void* ctx, zx_handle_t handle, double data) {
        auto ret = static_cast<D*>(ctx)->Apifloat64(zx::handle(handle), data);
        return ret;
    }
    static zx_status_t Apiduration(void* ctx, zx_handle_t handle, zx_duration_t data) {
        auto ret = static_cast<D*>(ctx)->Apiduration(zx::handle(handle), data);
        return ret;
    }
    static zx_status_t Apikoid(void* ctx, zx_handle_t handle, zx_koid_t data) {
        auto ret = static_cast<D*>(ctx)->Apikoid(zx::handle(handle), data);
        return ret;
    }
    static zx_status_t Apipaddr(void* ctx, zx_handle_t handle, zx_paddr_t data) {
        auto ret = static_cast<D*>(ctx)->Apipaddr(zx::handle(handle), data);
        return ret;
    }
    static zx_status_t Apisignals(void* ctx, zx_handle_t handle, zx_signals_t data) {
        auto ret = static_cast<D*>(ctx)->Apisignals(zx::handle(handle), data);
        return ret;
    }
    static zx_status_t Apitime(void* ctx, zx_handle_t handle, zx_time_t data) {
        auto ret = static_cast<D*>(ctx)->Apitime(zx::handle(handle), data);
        return ret;
    }
    static zx_status_t Apivaddr(void* ctx, zx_handle_t handle, zx_vaddr_t data) {
        auto ret = static_cast<D*>(ctx)->Apivaddr(zx::handle(handle), data);
        return ret;
    }
    static zx_status_t Apioutput_bool(void* ctx, zx_handle_t handle, bool* out_result) {
        auto ret = static_cast<D*>(ctx)->Apioutput_bool(zx::handle(handle), out_result);
        return ret;
    }
    static zx_status_t Apioutput_int8(void* ctx, zx_handle_t handle, int8_t* out_result) {
        auto ret = static_cast<D*>(ctx)->Apioutput_int8(zx::handle(handle), out_result);
        return ret;
    }
    static zx_status_t Apioutput_int16(void* ctx, zx_handle_t handle, int16_t* out_result) {
        auto ret = static_cast<D*>(ctx)->Apioutput_int16(zx::handle(handle), out_result);
        return ret;
    }
    static zx_status_t Apioutput_int32(void* ctx, zx_handle_t handle, int32_t* out_result) {
        auto ret = static_cast<D*>(ctx)->Apioutput_int32(zx::handle(handle), out_result);
        return ret;
    }
    static zx_status_t Apioutput_int64(void* ctx, zx_handle_t handle, int64_t* out_result) {
        auto ret = static_cast<D*>(ctx)->Apioutput_int64(zx::handle(handle), out_result);
        return ret;
    }
    static zx_status_t Apioutput_uint8(void* ctx, zx_handle_t handle, uint8_t* out_result) {
        auto ret = static_cast<D*>(ctx)->Apioutput_uint8(zx::handle(handle), out_result);
        return ret;
    }
    static zx_status_t Apioutput_uint16(void* ctx, zx_handle_t handle, uint16_t* out_result) {
        auto ret = static_cast<D*>(ctx)->Apioutput_uint16(zx::handle(handle), out_result);
        return ret;
    }
    static zx_status_t Apioutput_uint32(void* ctx, zx_handle_t handle, uint32_t* out_result) {
        auto ret = static_cast<D*>(ctx)->Apioutput_uint32(zx::handle(handle), out_result);
        return ret;
    }
    static zx_status_t Apioutput_uint64(void* ctx, zx_handle_t handle, uint64_t* out_result) {
        auto ret = static_cast<D*>(ctx)->Apioutput_uint64(zx::handle(handle), out_result);
        return ret;
    }
    static zx_status_t Apioutput_float32(void* ctx, zx_handle_t handle, float* out_result) {
        auto ret = static_cast<D*>(ctx)->Apioutput_float32(zx::handle(handle), out_result);
        return ret;
    }
    static zx_status_t Apioutput_float64(void* ctx, zx_handle_t handle, double* out_result) {
        auto ret = static_cast<D*>(ctx)->Apioutput_float64(zx::handle(handle), out_result);
        return ret;
    }
    static zx_status_t Apioutput_duration(void* ctx, zx_handle_t handle, zx_duration_t* out_result) {
        auto ret = static_cast<D*>(ctx)->Apioutput_duration(zx::handle(handle), out_result);
        return ret;
    }
    static zx_status_t Apioutput_koid(void* ctx, zx_handle_t handle, zx_koid_t* out_result) {
        auto ret = static_cast<D*>(ctx)->Apioutput_koid(zx::handle(handle), out_result);
        return ret;
    }
    static zx_status_t Apioutput_paddr(void* ctx, zx_handle_t handle, zx_paddr_t* out_result) {
        auto ret = static_cast<D*>(ctx)->Apioutput_paddr(zx::handle(handle), out_result);
        return ret;
    }
    static zx_status_t Apioutput_signals(void* ctx, zx_handle_t handle, zx_signals_t* out_result) {
        auto ret = static_cast<D*>(ctx)->Apioutput_signals(zx::handle(handle), out_result);
        return ret;
    }
    static zx_status_t Apioutput_time(void* ctx, zx_handle_t handle, zx_time_t* out_result) {
        auto ret = static_cast<D*>(ctx)->Apioutput_time(zx::handle(handle), out_result);
        return ret;
    }
    static zx_status_t Apioutput_vaddr(void* ctx, zx_handle_t handle, zx_vaddr_t* out_result) {
        auto ret = static_cast<D*>(ctx)->Apioutput_vaddr(zx::handle(handle), out_result);
        return ret;
    }
    static void Apireturn_void(void* ctx, zx_handle_t handle) {
        static_cast<D*>(ctx)->Apireturn_void(zx::handle(handle));
    }
    static zx_status_t Apireturn_status(void* ctx, zx_handle_t handle) {
        auto ret = static_cast<D*>(ctx)->Apireturn_status(zx::handle(handle));
        return ret;
    }
    static zx_ticks_t Apireturn_ticks(void* ctx, zx_handle_t handle) {
        auto ret = static_cast<D*>(ctx)->Apireturn_ticks(zx::handle(handle));
        return ret;
    }
    static zx_time_t Apireturn_time(void* ctx, zx_handle_t handle) {
        auto ret = static_cast<D*>(ctx)->Apireturn_time(zx::handle(handle));
        return ret;
    }
    static uint32_t Apireturn_uint32(void* ctx, zx_handle_t handle) {
        auto ret = static_cast<D*>(ctx)->Apireturn_uint32(zx::handle(handle));
        return ret;
    }
    static uint64_t Apireturn_uint64(void* ctx, zx_handle_t handle) {
        auto ret = static_cast<D*>(ctx)->Apireturn_uint64(zx::handle(handle));
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

    ApiProtocolClient(zx_device_t* parent, const char* fragment_name) {
        api_protocol_t proto;
        if (device_get_fragment_protocol(parent, fragment_name, ZX_PROTOCOL_API, &proto) == ZX_OK) {
            ops_ = proto.ops;
            ctx_ = proto.ctx;
        } else {
            ops_ = nullptr;
            ctx_ = nullptr;
        }
    }

    // Create a ApiProtocolClient from the given parent device + "fragment".
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

    // Create a ApiProtocolClient from the given parent device.
    //
    // If ZX_OK is returned, the created object will be initialized in |result|.
    static zx_status_t CreateFromDevice(zx_device_t* parent, const char* fragment_name,
                                        ApiProtocolClient* result) {
        api_protocol_t proto;
        zx_status_t status = device_get_fragment_protocol(parent, fragment_name,
                                 ZX_PROTOCOL_API, &proto);
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

    zx_status_t bool(zx::handle handle, bool data) const {
        return ops_->bool(ctx_, handle.release(), data);
    }

    zx_status_t int8(zx::handle handle, int8_t data) const {
        return ops_->int8(ctx_, handle.release(), data);
    }

    zx_status_t int16(zx::handle handle, int16_t data) const {
        return ops_->int16(ctx_, handle.release(), data);
    }

    zx_status_t int32(zx::handle handle, int32_t data) const {
        return ops_->int32(ctx_, handle.release(), data);
    }

    zx_status_t int64(zx::handle handle, int64_t data) const {
        return ops_->int64(ctx_, handle.release(), data);
    }

    zx_status_t uint8(zx::handle handle, uint8_t data) const {
        return ops_->uint8(ctx_, handle.release(), data);
    }

    zx_status_t uint16(zx::handle handle, uint16_t data) const {
        return ops_->uint16(ctx_, handle.release(), data);
    }

    zx_status_t uint32(zx::handle handle, uint32_t data) const {
        return ops_->uint32(ctx_, handle.release(), data);
    }

    zx_status_t uint64(zx::handle handle, uint64_t data) const {
        return ops_->uint64(ctx_, handle.release(), data);
    }

    zx_status_t float32(zx::handle handle, float data) const {
        return ops_->float32(ctx_, handle.release(), data);
    }

    zx_status_t float64(zx::handle handle, double data) const {
        return ops_->float64(ctx_, handle.release(), data);
    }

    zx_status_t duration(zx::handle handle, zx_duration_t data) const {
        return ops_->duration(ctx_, handle.release(), data);
    }

    zx_status_t koid(zx::handle handle, zx_koid_t data) const {
        return ops_->koid(ctx_, handle.release(), data);
    }

    zx_status_t paddr(zx::handle handle, zx_paddr_t data) const {
        return ops_->paddr(ctx_, handle.release(), data);
    }

    zx_status_t signals(zx::handle handle, zx_signals_t data) const {
        return ops_->signals(ctx_, handle.release(), data);
    }

    zx_status_t time(zx::handle handle, zx_time_t data) const {
        return ops_->time(ctx_, handle.release(), data);
    }

    zx_status_t vaddr(zx::handle handle, zx_vaddr_t data) const {
        return ops_->vaddr(ctx_, handle.release(), data);
    }

    zx_status_t output_bool(zx::handle handle, bool* out_result) const {
        return ops_->output_bool(ctx_, handle.release(), out_result);
    }

    zx_status_t output_int8(zx::handle handle, int8_t* out_result) const {
        return ops_->output_int8(ctx_, handle.release(), out_result);
    }

    zx_status_t output_int16(zx::handle handle, int16_t* out_result) const {
        return ops_->output_int16(ctx_, handle.release(), out_result);
    }

    zx_status_t output_int32(zx::handle handle, int32_t* out_result) const {
        return ops_->output_int32(ctx_, handle.release(), out_result);
    }

    zx_status_t output_int64(zx::handle handle, int64_t* out_result) const {
        return ops_->output_int64(ctx_, handle.release(), out_result);
    }

    zx_status_t output_uint8(zx::handle handle, uint8_t* out_result) const {
        return ops_->output_uint8(ctx_, handle.release(), out_result);
    }

    zx_status_t output_uint16(zx::handle handle, uint16_t* out_result) const {
        return ops_->output_uint16(ctx_, handle.release(), out_result);
    }

    zx_status_t output_uint32(zx::handle handle, uint32_t* out_result) const {
        return ops_->output_uint32(ctx_, handle.release(), out_result);
    }

    zx_status_t output_uint64(zx::handle handle, uint64_t* out_result) const {
        return ops_->output_uint64(ctx_, handle.release(), out_result);
    }

    zx_status_t output_float32(zx::handle handle, float* out_result) const {
        return ops_->output_float32(ctx_, handle.release(), out_result);
    }

    zx_status_t output_float64(zx::handle handle, double* out_result) const {
        return ops_->output_float64(ctx_, handle.release(), out_result);
    }

    zx_status_t output_duration(zx::handle handle, zx_duration_t* out_result) const {
        return ops_->output_duration(ctx_, handle.release(), out_result);
    }

    zx_status_t output_koid(zx::handle handle, zx_koid_t* out_result) const {
        return ops_->output_koid(ctx_, handle.release(), out_result);
    }

    zx_status_t output_paddr(zx::handle handle, zx_paddr_t* out_result) const {
        return ops_->output_paddr(ctx_, handle.release(), out_result);
    }

    zx_status_t output_signals(zx::handle handle, zx_signals_t* out_result) const {
        return ops_->output_signals(ctx_, handle.release(), out_result);
    }

    zx_status_t output_time(zx::handle handle, zx_time_t* out_result) const {
        return ops_->output_time(ctx_, handle.release(), out_result);
    }

    zx_status_t output_vaddr(zx::handle handle, zx_vaddr_t* out_result) const {
        return ops_->output_vaddr(ctx_, handle.release(), out_result);
    }

    void return_void(zx::handle handle) const {
        ops_->return_void(ctx_, handle.release());
    }

    zx_status_t return_status(zx::handle handle) const {
        return ops_->return_status(ctx_, handle.release());
    }

    zx_ticks_t return_ticks(zx::handle handle) const {
        return ops_->return_ticks(ctx_, handle.release());
    }

    zx_time_t return_time(zx::handle handle) const {
        return ops_->return_time(ctx_, handle.release());
    }

    uint32_t return_uint32(zx::handle handle) const {
        return ops_->return_uint32(ctx_, handle.release());
    }

    uint64_t return_uint64(zx::handle handle) const {
        return ops_->return_uint64(ctx_, handle.release());
    }

private:
    api_protocol_ops_t* ops_;
    void* ctx_;
};

} // namespace ddk
