// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// WARNING: THIS FILE IS MACHINE GENERATED. DO NOT EDIT.
//          MODIFY system/fidl/protocols/intel_hda_codec.fidl INSTEAD.

#pragma once

#include <zircon/compiler.h>
#include <zircon/types.h>

__BEGIN_CDECLS;

// Forward declarations

typedef struct ihda_codec_protocol ihda_codec_protocol_t;

// Declarations

typedef struct ihda_codec_protocol_ops {
    zx_status_t (*get_driver_channel)(void* ctx, zx_handle_t* out_channel);
} ihda_codec_protocol_ops_t;

struct ihda_codec_protocol {
    ihda_codec_protocol_ops_t* ops;
    void* ctx;
};

// Fetch a zx_handle_t to a channel which can be used to communicate with the codec device.
static inline zx_status_t ihda_codec_get_driver_channel(const ihda_codec_protocol_t* proto,
                                                        zx_handle_t* out_channel) {
    return proto->ops->get_driver_channel(proto->ctx, out_channel);
}

__END_CDECLS;
