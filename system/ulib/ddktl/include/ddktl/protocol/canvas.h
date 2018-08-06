// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <ddk/protocol/canvas.h>
#include <ddktl/device-internal.h>
#include <zircon/assert.h>

#include "canvas-internal.h"

// DDK canvas protocol support.
//
// :: Proxies ::
//
// ddk::CanvasProtocolProxy is a simple wrappers around canvas_protocol_t. It does
// not own the pointers passed to it.
//
// :: Mixins ::
//
// ddk::CanvasProtocol is a mixin class that simplifies writing DDK drivers that
// implement the canvas protocol.
//
// :: Examples ::
//
// // A driver that implements a ZX_PROTOCOL_CANVAS device.
// class CanvasDevice;
// using CanvasDeviceType = ddk::Device<CanvasDevice, /* ddk mixins */>;
//
// class CanvasDevice : public CanvasDeviceType,
//                      public ddk::CanvasProtocol<CanvasDevice> {
//   public:
//     CanvasDevice(zx_device_t* parent)
//       : CanvasDeviceType("my-canvas-device", parent) {}
//
//    zx_status_t CanvasConfig(zx_handle_t vmo, size_t offset, canvas_info_t* info,
//                             uint8_t* canvas_idx);
//    zx_status_t CanvasFree(uint8_t canvas_idx);
//     ...
// };

namespace ddk {

template <typename D>
class CanvasProtocol {
public:
    CanvasProtocol() {
        internal::CheckCanvasProtocolSubclass<D>();
        canvas_proto_ops_.config = CanvasConfig;
        canvas_proto_ops_.free = CanvasFree;
    }

protected:
    canvas_protocol_ops_t canvas_proto_ops_ = {};

private:
    static zx_status_t CanvasConfig(void* ctx, zx_handle_t vmo, size_t offset, canvas_info_t* info,
                                    uint8_t* canvas_idx) {
        return static_cast<D*>(ctx)->CanvasConfig(vmo, offset, info, canvas_idx);
    }
    static zx_status_t CanvasFree(void* ctx, uint8_t canvas_idx) {
        return static_cast<D*>(ctx)->CanvasFree(canvas_idx);
    }
};

class CanvasProtocolProxy {
public:
    CanvasProtocolProxy(canvas_protocol_t* proto)
        : ops_(proto->ops), ctx_(proto->ctx) {}

    zx_status_t CanvasConfig(zx_handle_t vmo, size_t offset, canvas_info_t* info,
                             uint8_t* canvas_idx) {
        return ops_->config(ctx_, vmo, offset, info, canvas_idx);
    }
    zx_status_t CanvasFree(uint8_t canvas_idx) {
        return ops_->free(ctx_, canvas_idx);
    }

private:
    canvas_protocol_ops_t* ops_;
    void* ctx_;
};

} // namespace ddk
