// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include "ddktl/protocol/display-internal.h"

#include <ddk/protocol/display.h>
#include <zircon/assert.h>

namespace ddk {

template <typename D>
class DisplayProtocol : public internal::base_protocol {
  public:
    DisplayProtocol() {
        internal::CheckDisplayProtocolSubclass<D>();
        ops_.set_mode = SetMode;
        ops_.get_mode = GetMode;
        ops_.get_framebuffer = GetFramebuffer;
        ops_.flush = FlushThunk;

        // Can only inherit from one base_protocol implemenation
        ZX_ASSERT(ddk_proto_id_ == 0);
        ddk_proto_id_ = ZX_PROTOCOL_DISPLAY;
        ddk_proto_ops_ = &ops_;
    }

  private:
    static zx_status_t SetMode(void* ctx, zx_display_info_t* info) {
        return static_cast<D*>(ctx)->SetMode(info);
    }

    static zx_status_t GetMode(void* ctx, zx_display_info_t* info) {
        return static_cast<D*>(ctx)->GetMode(info);
    }

    static zx_status_t GetFramebuffer(void* ctx, void** framebuffer) {
        return static_cast<D*>(ctx)->GetFramebuffer(framebuffer);
    }

    static void FlushThunk(void* ctx) {
        static_cast<D*>(ctx)->Flush();
    }

    display_protocol_ops_t ops_ = {};
};

}  // namespace ddk
