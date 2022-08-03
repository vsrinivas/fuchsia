// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// WARNING: THIS FILE IS MACHINE GENERATED. DO NOT EDIT.
// Generated from the banjo.examples.simple banjo file

#pragma once

#include <banjo/examples/simple/c/banjo.h>
#include <ddktl/device-internal.h>
#include <lib/ddk/device.h>
#include <lib/ddk/driver.h>
#include <lib/zx/vmo.h>
#include <zircon/assert.h>
#include <zircon/compiler.h>
#include <zircon/types.h>

#include "banjo-internal.h"

// DDK simple-protocol support
//
// :: Proxies ::
//
// ddk::DrawingProtocolClient is a simple wrapper around
// drawing_protocol_t. It does not own the pointers passed to it.
//
// :: Mixins ::
//
// ddk::DrawingProtocol is a mixin class that simplifies writing DDK drivers
// that implement the drawing protocol. It doesn't set the base protocol.
//
// :: Examples ::
//
// // A driver that implements a ZX_PROTOCOL_DRAWING device.
// class DrawingDevice;
// using DrawingDeviceType = ddk::Device<DrawingDevice, /* ddk mixins */>;
//
// class DrawingDevice : public DrawingDeviceType,
//                      public ddk::DrawingProtocol<DrawingDevice> {
//   public:
//     DrawingDevice(zx_device_t* parent)
//         : DrawingDeviceType(parent) {}
//
//     void DrawingDraw(const point_t* p, direction_t d);
//
//     zx_status_t DrawingDrawLots(zx::vmo commands, point_t* out_p);
//
//     zx_status_t DrawingDrawArray(const point_t points[4]);
//
//     void DrawingDescribe(const char* one, char* out_two, size_t two_capacity);
//
//     ...
// };

namespace ddk {

template <typename D, typename Base = internal::base_mixin>
class DrawingProtocol : public Base {
public:
    DrawingProtocol() {
        internal::CheckDrawingProtocolSubclass<D>();
        drawing_protocol_ops_.draw = DrawingDraw;
        drawing_protocol_ops_.draw_lots = DrawingDrawLots;
        drawing_protocol_ops_.draw_array = DrawingDrawArray;
        drawing_protocol_ops_.describe = DrawingDescribe;

        if constexpr (internal::is_base_proto<Base>::value) {
            auto dev = static_cast<D*>(this);
            // Can only inherit from one base_protocol implementation.
            ZX_ASSERT(dev->ddk_proto_id_ == 0);
            dev->ddk_proto_id_ = ZX_PROTOCOL_DRAWING;
            dev->ddk_proto_ops_ = &drawing_protocol_ops_;
        }
    }

protected:
    drawing_protocol_ops_t drawing_protocol_ops_ = {};

private:
    static void DrawingDraw(void* ctx, const point_t* p, direction_t d) {
        static_cast<D*>(ctx)->DrawingDraw(p, d);
    }
    static zx_status_t DrawingDrawLots(void* ctx, zx_handle_t commands, point_t* out_p) {
        auto ret = static_cast<D*>(ctx)->DrawingDrawLots(zx::vmo(commands), out_p);
        return ret;
    }
    static zx_status_t DrawingDrawArray(void* ctx, const point_t points[4]) {
        auto ret = static_cast<D*>(ctx)->DrawingDrawArray(points);
        return ret;
    }
    static void DrawingDescribe(void* ctx, const char* one, char* out_two, size_t two_capacity) {
        static_cast<D*>(ctx)->DrawingDescribe(one, out_two, two_capacity);
    }
};

class DrawingProtocolClient {
public:
    DrawingProtocolClient()
        : ops_(nullptr), ctx_(nullptr) {}
    DrawingProtocolClient(const drawing_protocol_t* proto)
        : ops_(proto->ops), ctx_(proto->ctx) {}

    DrawingProtocolClient(zx_device_t* parent) {
        drawing_protocol_t proto;
        if (device_get_protocol(parent, ZX_PROTOCOL_DRAWING, &proto) == ZX_OK) {
            ops_ = proto.ops;
            ctx_ = proto.ctx;
        } else {
            ops_ = nullptr;
            ctx_ = nullptr;
        }
    }

    DrawingProtocolClient(zx_device_t* parent, const char* fragment_name) {
        drawing_protocol_t proto;
        if (device_get_fragment_protocol(parent, fragment_name, ZX_PROTOCOL_DRAWING, &proto) == ZX_OK) {
            ops_ = proto.ops;
            ctx_ = proto.ctx;
        } else {
            ops_ = nullptr;
            ctx_ = nullptr;
        }
    }

    // Create a DrawingProtocolClient from the given parent device + "fragment".
    //
    // If ZX_OK is returned, the created object will be initialized in |result|.
    static zx_status_t CreateFromDevice(zx_device_t* parent,
                                        DrawingProtocolClient* result) {
        drawing_protocol_t proto;
        zx_status_t status = device_get_protocol(
                parent, ZX_PROTOCOL_DRAWING, &proto);
        if (status != ZX_OK) {
            return status;
        }
        *result = DrawingProtocolClient(&proto);
        return ZX_OK;
    }

    // Create a DrawingProtocolClient from the given parent device.
    //
    // If ZX_OK is returned, the created object will be initialized in |result|.
    static zx_status_t CreateFromDevice(zx_device_t* parent, const char* fragment_name,
                                        DrawingProtocolClient* result) {
        drawing_protocol_t proto;
        zx_status_t status = device_get_fragment_protocol(parent, fragment_name,
                                 ZX_PROTOCOL_DRAWING, &proto);
        if (status != ZX_OK) {
            return status;
        }
        *result = DrawingProtocolClient(&proto);
        return ZX_OK;
    }

    void GetProto(drawing_protocol_t* proto) const {
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

    void Draw(const point_t* p, direction_t d) const {
        ops_->draw(ctx_, p, d);
    }

    zx_status_t DrawLots(zx::vmo commands, point_t* out_p) const {
        return ops_->draw_lots(ctx_, commands.release(), out_p);
    }

    zx_status_t DrawArray(const point_t points[4]) const {
        return ops_->draw_array(ctx_, points);
    }

    void Describe(const char* one, char* out_two, size_t two_capacity) const {
        ops_->describe(ctx_, one, out_two, two_capacity);
    }

private:
    const drawing_protocol_ops_t* ops_;
    void* ctx_;
};

} // namespace ddk
