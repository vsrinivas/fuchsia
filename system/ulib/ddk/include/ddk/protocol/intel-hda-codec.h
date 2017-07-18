// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <magenta/compiler.h>
#include <magenta/types.h>

__BEGIN_CDECLS;

typedef struct ihda_codec_protocol_ops {
    // Fetch an mx_handle_t to a channel which can be used to communicate with the codec device.
    mx_status_t (*get_driver_channel)(void* ctx, mx_handle_t* channel_out);
} ihda_codec_protocol_ops_t;

typedef struct ihda_codec_protocol {
    ihda_codec_protocol_ops_t* ops;
    void* ctx;
} ihda_codec_protocol_t;

__END_CDECLS;

