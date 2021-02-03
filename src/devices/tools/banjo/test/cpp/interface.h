// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// WARNING: THIS FILE IS MACHINE GENERATED. DO NOT EDIT.
// Generated from the banjo.examples.interface banjo file

#pragma once

#include <banjo/examples/interface.h>
#include <ddk/device.h>
#include <ddk/driver.h>
#include <ddktl/device-internal.h>
#include <zircon/assert.h>
#include <zircon/compiler.h>
#include <zircon/types.h>

#include "banjo-internal.h"

// DDK interface-protocol support
//
// :: Proxies ::
//
// ddk::BakerProtocolClient is a simple wrapper around
// baker_protocol_t. It does not own the pointers passed to it.
//
// :: Mixins ::
//
// ddk::BakerProtocol is a mixin class that simplifies writing DDK drivers
// that implement the baker protocol. It doesn't set the base protocol.
//
// :: Examples ::
//
// // A driver that implements a ZX_PROTOCOL_BAKER device.
// class BakerDevice;
// using BakerDeviceType = ddk::Device<BakerDevice, /* ddk mixins */>;
//
// class BakerDevice : public BakerDeviceType,
//                      public ddk::BakerProtocol<BakerDevice> {
//   public:
//     BakerDevice(zx_device_t* parent)
//         : BakerDeviceType(parent) {}
//
//     void BakerRegister(const cookie_maker_protocol_t* intf);
//
//     void BakerDeRegister();
//
//     ...
// };

namespace ddk {
// An interface for a device that's able to create and deliver cookies!

template <typename D>
class CookieMakerProtocol : public internal::base_mixin {
public:
    CookieMakerProtocol() {
        internal::CheckCookieMakerProtocolSubclass<D>();
        cookie_maker_protocol_ops_.prep = CookieMakerPrep;
        cookie_maker_protocol_ops_.bake = CookieMakerBake;
        cookie_maker_protocol_ops_.deliver = CookieMakerDeliver;
    }

protected:
    cookie_maker_protocol_ops_t cookie_maker_protocol_ops_ = {};

private:
    // Asynchonously preps a cookie.
    static void CookieMakerPrep(void* ctx, cookie_kind_t cookie, cookie_maker_prep_callback callback, void* cookie) {
        static_cast<D*>(ctx)->CookieMakerPrep(cookie, callback, cookie);
    }
    // Asynchonously bakes a cookie.
    // Must only be called after preping finishes.
    static void CookieMakerBake(void* ctx, uint64_t token, zx_time_t time, cookie_maker_bake_callback callback, void* cookie) {
        static_cast<D*>(ctx)->CookieMakerBake(token, time, callback, cookie);
    }
    // Synchronously deliver a cookie.
    // Must be called only after Bake finishes.
    static zx_status_t CookieMakerDeliver(void* ctx, uint64_t token) {
        auto ret = static_cast<D*>(ctx)->CookieMakerDeliver(token);
        return ret;
    }
};

class CookieMakerProtocolClient {
public:
    CookieMakerProtocolClient()
        : ops_(nullptr), ctx_(nullptr) {}
    CookieMakerProtocolClient(const cookie_maker_protocol_t* proto)
        : ops_(proto->ops), ctx_(proto->ctx) {}

    void GetProto(cookie_maker_protocol_t* proto) const {
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

    // Asynchonously preps a cookie.
    void Prep(cookie_kind_t cookie, cookie_maker_prep_callback callback, void* cookie) const {
        ops_->prep(ctx_, cookie, callback, cookie);
    }

    // Asynchonously bakes a cookie.
    // Must only be called after preping finishes.
    void Bake(uint64_t token, zx_time_t time, cookie_maker_bake_callback callback, void* cookie) const {
        ops_->bake(ctx_, token, time, callback, cookie);
    }

    // Synchronously deliver a cookie.
    // Must be called only after Bake finishes.
    zx_status_t Deliver(uint64_t token) const {
        return ops_->deliver(ctx_, token);
    }

private:
    cookie_maker_protocol_ops_t* ops_;
    void* ctx_;
};
// Protocol for a baker who outsources all of it's baking duties to others.

template <typename D, typename Base = internal::base_mixin>
class BakerProtocol : public Base {
public:
    BakerProtocol() {
        internal::CheckBakerProtocolSubclass<D>();
        baker_protocol_ops_.register = BakerRegister;
        baker_protocol_ops_.de_register = BakerDeRegister;

        if constexpr (internal::is_base_proto<Base>::value) {
            auto dev = static_cast<D*>(this);
            // Can only inherit from one base_protocol implementation.
            ZX_ASSERT(dev->ddk_proto_id_ == 0);
            dev->ddk_proto_id_ = ZX_PROTOCOL_BAKER;
            dev->ddk_proto_ops_ = &baker_protocol_ops_;
        }
    }

protected:
    baker_protocol_ops_t baker_protocol_ops_ = {};

private:
    // Registers a cookie maker device which the baker can use.
    static void BakerRegister(void* ctx, const cookie_maker_protocol_t* intf) {
        static_cast<D*>(ctx)->BakerRegister(intf);
    }
    // De-registers a cookie maker device when it's no longer available.
    static void BakerDeRegister(void* ctx) {
        static_cast<D*>(ctx)->BakerDeRegister();
    }
};

class BakerProtocolClient {
public:
    BakerProtocolClient()
        : ops_(nullptr), ctx_(nullptr) {}
    BakerProtocolClient(const baker_protocol_t* proto)
        : ops_(proto->ops), ctx_(proto->ctx) {}

    BakerProtocolClient(zx_device_t* parent) {
        baker_protocol_t proto;
        if (device_get_protocol(parent, ZX_PROTOCOL_BAKER, &proto) == ZX_OK) {
            ops_ = proto.ops;
            ctx_ = proto.ctx;
        } else {
            ops_ = nullptr;
            ctx_ = nullptr;
        }
    }

    BakerProtocolClient(zx_device_t* parent, const char* fragment_name) {
        zx_device_t* fragment;
        bool found = device_get_fragment(parent, fragment_name, &fragment);
        baker_protocol_t proto;
        if (found && device_get_protocol(fragment, ZX_PROTOCOL_BAKER, &proto) == ZX_OK) {
            ops_ = proto.ops;
            ctx_ = proto.ctx;
        } else {
            ops_ = nullptr;
            ctx_ = nullptr;
        }
    }

    // Create a BakerProtocolClient from the given parent device + "fragment".
    //
    // If ZX_OK is returned, the created object will be initialized in |result|.
    static zx_status_t CreateFromDevice(zx_device_t* parent,
                                        BakerProtocolClient* result) {
        baker_protocol_t proto;
        zx_status_t status = device_get_protocol(
                parent, ZX_PROTOCOL_BAKER, &proto);
        if (status != ZX_OK) {
            return status;
        }
        *result = BakerProtocolClient(&proto);
        return ZX_OK;
    }

    // Create a BakerProtocolClient from the given parent device.
    //
    // If ZX_OK is returned, the created object will be initialized in |result|.
    static zx_status_t CreateFromDevice(zx_device_t* parent, const char* fragment_name,
                                        BakerProtocolClient* result) {
        zx_device_t* fragment;
        bool found = device_get_fragment(parent, fragment_name, &fragment);
        if (!found) {
          return ZX_ERR_NOT_FOUND;
        }
        return CreateFromDevice(fragment, result);
    }

    void GetProto(baker_protocol_t* proto) const {
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

    // Registers a cookie maker device which the baker can use.
    void Register(void* intf_ctx, cookie_maker_protocol_ops_t* intf_ops) const {
        const cookie_maker_protocol_t intf2 = {
            .ops = intf_ops,
            .ctx = intf_ctx,
        };
        const cookie_maker_protocol_t* intf = &intf2;
        ops_->register(ctx_, intf);
    }

    // De-registers a cookie maker device when it's no longer available.
    void DeRegister() const {
        ops_->de_register(ctx_);
    }

private:
    baker_protocol_ops_t* ops_;
    void* ctx_;
};

} // namespace ddk
