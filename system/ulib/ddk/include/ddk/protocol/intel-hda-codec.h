// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <zircon/compiler.h>
#include <zircon/types.h>

__BEGIN_CDECLS;

typedef struct ihda_codec_protocol_ops {
    // Fetch an zx_handle_t to a channel which can be used to communicate with the codec device.
    zx_status_t (*get_driver_channel)(void* ctx, zx_handle_t* channel_out);
} ihda_codec_protocol_ops_t;

typedef struct ihda_codec_protocol {
    ihda_codec_protocol_ops_t* ops;
    void* ctx;
} ihda_codec_protocol_t;

__END_CDECLS;

