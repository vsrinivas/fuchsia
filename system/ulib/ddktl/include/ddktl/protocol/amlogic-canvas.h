// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// WARNING: THIS FILE IS MACHINE GENERATED. DO NOT EDIT.
//          MODIFY system/fidl/protocols/amlogic_canvas.banjo INSTEAD.

#pragma once

#include <ddk/protocol/amlogic-canvas.h>
#include <ddktl/device-internal.h>
#include <zircon/assert.h>
#include <zircon/compiler.h>
#include <zircon/types.h>

#include "amlogic-canvas-internal.h"

// DDK canvas-protocol support
//
// :: Proxies ::
//
// ddk::CanvasProtocolProxy is a simple wrapper around
// canvas_protocol_t. It does not own the pointers passed to it
//
// :: Mixins ::
//
// ddk::CanvasProtocol is a mixin class that simplifies writing DDK drivers
// that implement the canvas protocol. It doesn't set the base protocol.
//
// :: Examples ::
//
// // A driver that implements a ZX_PROTOCOL_CANVAS device.
// class CanvasDevice {
// using CanvasDeviceType = ddk::Device<CanvasDevice, /* ddk mixins */>;
//
// class CanvasDevice : public CanvasDeviceType,
//                      public ddk::CanvasProtocol<CanvasDevice> {
//   public:
//     CanvasDevice(zx_device_t* parent)
//         : CanvasDeviceType("my-canvas-protocol-device", parent) {}
//
//     zx_status_t CanvasConfig(zx_handle_t vmo, size_t offset, const canvas_info_t* info, uint8_t*
//     out_canvas_idx);
//
//     zx_status_t CanvasFree(uint8_t canvas_idx);
//
//     ...
// };

namespace ddk {

template <typename D>
class CanvasProtocol : public internal::base_protocol {
public:
    CanvasProtocol() {
        internal::CheckCanvasProtocolSubclass<D>();
        ops_.config = CanvasConfig;
        ops_.free = CanvasFree;

        // Can only inherit from one base_protocol implementation.
        ZX_ASSERT(ddk_proto_id_ == 0);
        ddk_proto_id_ = ZX_PROTOCOL_CANVAS;
        ddk_proto_ops_ = &ops_;
    }

protected:
    canvas_protocol_ops_t ops_ = {};

private:
    // Configures a canvas.
    // Adds a framebuffer to the canvas lookup table.
    static zx_status_t CanvasConfig(void* ctx, zx_handle_t vmo, size_t offset,
                                    const canvas_info_t* info, uint8_t* out_canvas_idx) {
        return static_cast<D*>(ctx)->CanvasConfig(vmo, offset, info, out_canvas_idx);
    }
    // Frees up a canvas.
    static zx_status_t CanvasFree(void* ctx, uint8_t canvas_idx) {
        return static_cast<D*>(ctx)->CanvasFree(canvas_idx);
    }
};

class CanvasProtocolProxy {
public:
    CanvasProtocolProxy() : ops_(nullptr), ctx_(nullptr) {}
    CanvasProtocolProxy(const canvas_protocol_t* proto) : ops_(proto->ops), ctx_(proto->ctx) {}

    void GetProto(canvas_protocol_t* proto) {
        proto->ctx = ctx_;
        proto->ops = ops_;
    }
    bool is_valid() { return ops_ != nullptr; }
    void clear() {
        ctx_ = nullptr;
        ops_ = nullptr;
    }
    // Configures a canvas.
    // Adds a framebuffer to the canvas lookup table.
    zx_status_t Config(zx_handle_t vmo, size_t offset, const canvas_info_t* info,
                       uint8_t* out_canvas_idx) {
        return ops_->config(ctx_, vmo, offset, info, out_canvas_idx);
    }
    // Frees up a canvas.
    zx_status_t Free(uint8_t canvas_idx) { return ops_->free(ctx_, canvas_idx); }

private:
    canvas_protocol_ops_t* ops_;
    void* ctx_;
};

} // namespace ddk
