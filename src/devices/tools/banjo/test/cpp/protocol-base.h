// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// WARNING: THIS FILE IS MACHINE GENERATED. DO NOT EDIT.
// Generated from the banjo.examples.protocol.base banjo file

#pragma once

#include <banjo/examples/protocol/base.h>
#include <ddk/driver.h>
#include <ddktl/device-internal.h>
#include <zircon/assert.h>
#include <zircon/compiler.h>
#include <zircon/types.h>

#include "base-internal.h"

// DDK base-protocol support
//
// :: Proxies ::
//
// ddk::SynchronousBaseProtocolClient is a simple wrapper around
// synchronous_base_protocol_t. It does not own the pointers passed to it.
//
// :: Mixins ::
//
// ddk::SynchronousBaseProtocol is a mixin class that simplifies writing DDK drivers
// that implement the synchronous-base protocol. It doesn't set the base protocol.
//
// :: Examples ::
//
// // A driver that implements a ZX_PROTOCOL_SYNCHRONOUS_BASE device.
// class SynchronousBaseDevice;
// using SynchronousBaseDeviceType = ddk::Device<SynchronousBaseDevice, /* ddk mixins */>;
//
// class SynchronousBaseDevice : public SynchronousBaseDeviceType,
//                      public ddk::SynchronousBaseProtocol<SynchronousBaseDevice> {
//   public:
//     SynchronousBaseDevice(zx_device_t* parent)
//         : SynchronousBaseDeviceType(parent) {}
//
//     zx_status_t SynchronousBaseStatus(zx_status_t status, zx_status_t* out_status_2);
//
//     zx_time_t SynchronousBaseTime(zx_time_t time, zx_time_t* out_time_2);
//
//     zx_duration_t SynchronousBaseDuration(zx_duration_t duration, zx_duration_t* out_duration_2);
//
//     zx_koid_t SynchronousBaseKoid(zx_koid_t koid, zx_koid_t* out_koid_2);
//
//     zx_vaddr_t SynchronousBaseVaddr(zx_vaddr_t vaddr, zx_vaddr_t* out_vaddr_2);
//
//     zx_paddr_t SynchronousBasePaddr(zx_paddr_t paddr, zx_paddr_t* out_paddr_2);
//
//     zx_paddr32_t SynchronousBasePaddr32(zx_paddr32_t paddr32, zx_paddr32_t* out_paddr32_2);
//
//     zx_gpaddr_t SynchronousBaseGpaddr(zx_gpaddr_t gpaddr, zx_gpaddr_t* out_gpaddr_2);
//
//     zx_off_t SynchronousBaseOff(zx_off_t off, zx_off_t* out_off_2);
//
//     ...
// };
// :: Proxies ::
//
// ddk::AsyncBaseProtocolClient is a simple wrapper around
// async_base_protocol_t. It does not own the pointers passed to it.
//
// :: Mixins ::
//
// ddk::AsyncBaseProtocol is a mixin class that simplifies writing DDK drivers
// that implement the async-base protocol. It doesn't set the base protocol.
//
// :: Examples ::
//
// // A driver that implements a ZX_PROTOCOL_ASYNC_BASE device.
// class AsyncBaseDevice;
// using AsyncBaseDeviceType = ddk::Device<AsyncBaseDevice, /* ddk mixins */>;
//
// class AsyncBaseDevice : public AsyncBaseDeviceType,
//                      public ddk::AsyncBaseProtocol<AsyncBaseDevice> {
//   public:
//     AsyncBaseDevice(zx_device_t* parent)
//         : AsyncBaseDeviceType(parent) {}
//
//     void AsyncBaseStatus(zx_status_t status, async_base_status_callback callback, void* cookie);
//
//     void AsyncBaseTime(zx_time_t time, async_base_time_callback callback, void* cookie);
//
//     void AsyncBaseDuration(zx_duration_t duration, async_base_duration_callback callback, void* cookie);
//
//     void AsyncBaseKoid(zx_koid_t koid, async_base_koid_callback callback, void* cookie);
//
//     void AsyncBaseVaddr(zx_vaddr_t vaddr, async_base_vaddr_callback callback, void* cookie);
//
//     void AsyncBasePaddr(zx_paddr_t paddr, async_base_paddr_callback callback, void* cookie);
//
//     void AsyncBasePaddr32(zx_paddr32_t paddr32, async_base_paddr32_callback callback, void* cookie);
//
//     void AsyncBaseGpaddr(zx_gpaddr_t gpaddr, async_base_gpaddr_callback callback, void* cookie);
//
//     void AsyncBaseOff(zx_off_t off, async_base_off_callback callback, void* cookie);
//
//     ...
// };

namespace ddk {

template <typename D, typename Base = internal::base_mixin>
class SynchronousBaseProtocol : public Base {
public:
    SynchronousBaseProtocol() {
        internal::CheckSynchronousBaseProtocolSubclass<D>();
        synchronous_base_protocol_ops_.status = SynchronousBaseStatus;
        synchronous_base_protocol_ops_.time = SynchronousBaseTime;
        synchronous_base_protocol_ops_.duration = SynchronousBaseDuration;
        synchronous_base_protocol_ops_.koid = SynchronousBaseKoid;
        synchronous_base_protocol_ops_.vaddr = SynchronousBaseVaddr;
        synchronous_base_protocol_ops_.paddr = SynchronousBasePaddr;
        synchronous_base_protocol_ops_.paddr32 = SynchronousBasePaddr32;
        synchronous_base_protocol_ops_.gpaddr = SynchronousBaseGpaddr;
        synchronous_base_protocol_ops_.off = SynchronousBaseOff;

        if constexpr (internal::is_base_proto<Base>::value) {
            auto dev = static_cast<D*>(this);
            // Can only inherit from one base_protocol implementation.
            ZX_ASSERT(dev->ddk_proto_id_ == 0);
            dev->ddk_proto_id_ = ZX_PROTOCOL_SYNCHRONOUS_BASE;
            dev->ddk_proto_ops_ = &synchronous_base_protocol_ops_;
        }
    }

protected:
    synchronous_base_protocol_ops_t synchronous_base_protocol_ops_ = {};

private:
    static zx_status_t SynchronousBaseStatus(void* ctx, zx_status_t status, zx_status_t* out_status_2) {
        auto ret = static_cast<D*>(ctx)->SynchronousBaseStatus(status, out_status_2);
        return ret;
    }
    static zx_time_t SynchronousBaseTime(void* ctx, zx_time_t time, zx_time_t* out_time_2) {
        auto ret = static_cast<D*>(ctx)->SynchronousBaseTime(time, out_time_2);
        return ret;
    }
    static zx_duration_t SynchronousBaseDuration(void* ctx, zx_duration_t duration, zx_duration_t* out_duration_2) {
        auto ret = static_cast<D*>(ctx)->SynchronousBaseDuration(duration, out_duration_2);
        return ret;
    }
    static zx_koid_t SynchronousBaseKoid(void* ctx, zx_koid_t koid, zx_koid_t* out_koid_2) {
        auto ret = static_cast<D*>(ctx)->SynchronousBaseKoid(koid, out_koid_2);
        return ret;
    }
    static zx_vaddr_t SynchronousBaseVaddr(void* ctx, zx_vaddr_t vaddr, zx_vaddr_t* out_vaddr_2) {
        auto ret = static_cast<D*>(ctx)->SynchronousBaseVaddr(vaddr, out_vaddr_2);
        return ret;
    }
    static zx_paddr_t SynchronousBasePaddr(void* ctx, zx_paddr_t paddr, zx_paddr_t* out_paddr_2) {
        auto ret = static_cast<D*>(ctx)->SynchronousBasePaddr(paddr, out_paddr_2);
        return ret;
    }
    static zx_paddr32_t SynchronousBasePaddr32(void* ctx, zx_paddr32_t paddr32, zx_paddr32_t* out_paddr32_2) {
        auto ret = static_cast<D*>(ctx)->SynchronousBasePaddr32(paddr32, out_paddr32_2);
        return ret;
    }
    static zx_gpaddr_t SynchronousBaseGpaddr(void* ctx, zx_gpaddr_t gpaddr, zx_gpaddr_t* out_gpaddr_2) {
        auto ret = static_cast<D*>(ctx)->SynchronousBaseGpaddr(gpaddr, out_gpaddr_2);
        return ret;
    }
    static zx_off_t SynchronousBaseOff(void* ctx, zx_off_t off, zx_off_t* out_off_2) {
        auto ret = static_cast<D*>(ctx)->SynchronousBaseOff(off, out_off_2);
        return ret;
    }
};

class SynchronousBaseProtocolClient {
public:
    SynchronousBaseProtocolClient()
        : ops_(nullptr), ctx_(nullptr) {}
    SynchronousBaseProtocolClient(const synchronous_base_protocol_t* proto)
        : ops_(proto->ops), ctx_(proto->ctx) {}

    SynchronousBaseProtocolClient(zx_device_t* parent) {
        synchronous_base_protocol_t proto;
        if (device_get_protocol(parent, ZX_PROTOCOL_SYNCHRONOUS_BASE, &proto) == ZX_OK) {
            ops_ = proto.ops;
            ctx_ = proto.ctx;
        } else {
            ops_ = nullptr;
            ctx_ = nullptr;
        }
    }

    // Create a SynchronousBaseProtocolClient from the given parent device.
    //
    // If ZX_OK is returned, the created object will be initialized in |result|.
    static zx_status_t CreateFromDevice(zx_device_t* parent,
                                        SynchronousBaseProtocolClient* result) {
        synchronous_base_protocol_t proto;
        zx_status_t status = device_get_protocol(
                parent, ZX_PROTOCOL_SYNCHRONOUS_BASE, &proto);
        if (status != ZX_OK) {
            return status;
        }
        *result = SynchronousBaseProtocolClient(&proto);
        return ZX_OK;
    }

    void GetProto(synchronous_base_protocol_t* proto) const {
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

    zx_status_t Status(zx_status_t status, zx_status_t* out_status_2) const {
        return ops_->status(ctx_, status, out_status_2);
    }

    zx_time_t Time(zx_time_t time, zx_time_t* out_time_2) const {
        return ops_->time(ctx_, time, out_time_2);
    }

    zx_duration_t Duration(zx_duration_t duration, zx_duration_t* out_duration_2) const {
        return ops_->duration(ctx_, duration, out_duration_2);
    }

    zx_koid_t Koid(zx_koid_t koid, zx_koid_t* out_koid_2) const {
        return ops_->koid(ctx_, koid, out_koid_2);
    }

    zx_vaddr_t Vaddr(zx_vaddr_t vaddr, zx_vaddr_t* out_vaddr_2) const {
        return ops_->vaddr(ctx_, vaddr, out_vaddr_2);
    }

    zx_paddr_t Paddr(zx_paddr_t paddr, zx_paddr_t* out_paddr_2) const {
        return ops_->paddr(ctx_, paddr, out_paddr_2);
    }

    zx_paddr32_t Paddr32(zx_paddr32_t paddr32, zx_paddr32_t* out_paddr32_2) const {
        return ops_->paddr32(ctx_, paddr32, out_paddr32_2);
    }

    zx_gpaddr_t Gpaddr(zx_gpaddr_t gpaddr, zx_gpaddr_t* out_gpaddr_2) const {
        return ops_->gpaddr(ctx_, gpaddr, out_gpaddr_2);
    }

    zx_off_t Off(zx_off_t off, zx_off_t* out_off_2) const {
        return ops_->off(ctx_, off, out_off_2);
    }

private:
    synchronous_base_protocol_ops_t* ops_;
    void* ctx_;
};

template <typename D, typename Base = internal::base_mixin>
class AsyncBaseProtocol : public Base {
public:
    AsyncBaseProtocol() {
        internal::CheckAsyncBaseProtocolSubclass<D>();
        async_base_protocol_ops_.status = AsyncBaseStatus;
        async_base_protocol_ops_.time = AsyncBaseTime;
        async_base_protocol_ops_.duration = AsyncBaseDuration;
        async_base_protocol_ops_.koid = AsyncBaseKoid;
        async_base_protocol_ops_.vaddr = AsyncBaseVaddr;
        async_base_protocol_ops_.paddr = AsyncBasePaddr;
        async_base_protocol_ops_.paddr32 = AsyncBasePaddr32;
        async_base_protocol_ops_.gpaddr = AsyncBaseGpaddr;
        async_base_protocol_ops_.off = AsyncBaseOff;

        if constexpr (internal::is_base_proto<Base>::value) {
            auto dev = static_cast<D*>(this);
            // Can only inherit from one base_protocol implementation.
            ZX_ASSERT(dev->ddk_proto_id_ == 0);
            dev->ddk_proto_id_ = ZX_PROTOCOL_ASYNC_BASE;
            dev->ddk_proto_ops_ = &async_base_protocol_ops_;
        }
    }

protected:
    async_base_protocol_ops_t async_base_protocol_ops_ = {};

private:
    static void AsyncBaseStatus(void* ctx, zx_status_t status, async_base_status_callback callback, void* cookie) {
        static_cast<D*>(ctx)->AsyncBaseStatus(status, callback, cookie);
    }
    static void AsyncBaseTime(void* ctx, zx_time_t time, async_base_time_callback callback, void* cookie) {
        static_cast<D*>(ctx)->AsyncBaseTime(time, callback, cookie);
    }
    static void AsyncBaseDuration(void* ctx, zx_duration_t duration, async_base_duration_callback callback, void* cookie) {
        static_cast<D*>(ctx)->AsyncBaseDuration(duration, callback, cookie);
    }
    static void AsyncBaseKoid(void* ctx, zx_koid_t koid, async_base_koid_callback callback, void* cookie) {
        static_cast<D*>(ctx)->AsyncBaseKoid(koid, callback, cookie);
    }
    static void AsyncBaseVaddr(void* ctx, zx_vaddr_t vaddr, async_base_vaddr_callback callback, void* cookie) {
        static_cast<D*>(ctx)->AsyncBaseVaddr(vaddr, callback, cookie);
    }
    static void AsyncBasePaddr(void* ctx, zx_paddr_t paddr, async_base_paddr_callback callback, void* cookie) {
        static_cast<D*>(ctx)->AsyncBasePaddr(paddr, callback, cookie);
    }
    static void AsyncBasePaddr32(void* ctx, zx_paddr32_t paddr32, async_base_paddr32_callback callback, void* cookie) {
        static_cast<D*>(ctx)->AsyncBasePaddr32(paddr32, callback, cookie);
    }
    static void AsyncBaseGpaddr(void* ctx, zx_gpaddr_t gpaddr, async_base_gpaddr_callback callback, void* cookie) {
        static_cast<D*>(ctx)->AsyncBaseGpaddr(gpaddr, callback, cookie);
    }
    static void AsyncBaseOff(void* ctx, zx_off_t off, async_base_off_callback callback, void* cookie) {
        static_cast<D*>(ctx)->AsyncBaseOff(off, callback, cookie);
    }
};

class AsyncBaseProtocolClient {
public:
    AsyncBaseProtocolClient()
        : ops_(nullptr), ctx_(nullptr) {}
    AsyncBaseProtocolClient(const async_base_protocol_t* proto)
        : ops_(proto->ops), ctx_(proto->ctx) {}

    AsyncBaseProtocolClient(zx_device_t* parent) {
        async_base_protocol_t proto;
        if (device_get_protocol(parent, ZX_PROTOCOL_ASYNC_BASE, &proto) == ZX_OK) {
            ops_ = proto.ops;
            ctx_ = proto.ctx;
        } else {
            ops_ = nullptr;
            ctx_ = nullptr;
        }
    }

    // Create a AsyncBaseProtocolClient from the given parent device.
    //
    // If ZX_OK is returned, the created object will be initialized in |result|.
    static zx_status_t CreateFromDevice(zx_device_t* parent,
                                        AsyncBaseProtocolClient* result) {
        async_base_protocol_t proto;
        zx_status_t status = device_get_protocol(
                parent, ZX_PROTOCOL_ASYNC_BASE, &proto);
        if (status != ZX_OK) {
            return status;
        }
        *result = AsyncBaseProtocolClient(&proto);
        return ZX_OK;
    }

    void GetProto(async_base_protocol_t* proto) const {
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

    void Status(zx_status_t status, async_base_status_callback callback, void* cookie) const {
        ops_->status(ctx_, status, callback, cookie);
    }

    void Time(zx_time_t time, async_base_time_callback callback, void* cookie) const {
        ops_->time(ctx_, time, callback, cookie);
    }

    void Duration(zx_duration_t duration, async_base_duration_callback callback, void* cookie) const {
        ops_->duration(ctx_, duration, callback, cookie);
    }

    void Koid(zx_koid_t koid, async_base_koid_callback callback, void* cookie) const {
        ops_->koid(ctx_, koid, callback, cookie);
    }

    void Vaddr(zx_vaddr_t vaddr, async_base_vaddr_callback callback, void* cookie) const {
        ops_->vaddr(ctx_, vaddr, callback, cookie);
    }

    void Paddr(zx_paddr_t paddr, async_base_paddr_callback callback, void* cookie) const {
        ops_->paddr(ctx_, paddr, callback, cookie);
    }

    void Paddr32(zx_paddr32_t paddr32, async_base_paddr32_callback callback, void* cookie) const {
        ops_->paddr32(ctx_, paddr32, callback, cookie);
    }

    void Gpaddr(zx_gpaddr_t gpaddr, async_base_gpaddr_callback callback, void* cookie) const {
        ops_->gpaddr(ctx_, gpaddr, callback, cookie);
    }

    void Off(zx_off_t off, async_base_off_callback callback, void* cookie) const {
        ops_->off(ctx_, off, callback, cookie);
    }

private:
    async_base_protocol_ops_t* ops_;
    void* ctx_;
};

} // namespace ddk
