// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <ddk/driver.h>
#include <magenta/compiler.h>

__BEGIN_CDECLS;

typedef struct ihda_codec_protocol {
    // get_driver_channel
    //
    // Fetch an mx_handle_t to a channel which can be used to communicate with the codec device.
    mx_status_t (*get_driver_channel)(mx_device_t* codec_dev, mx_handle_t* channel_out);
} ihda_codec_protocol_t;

__END_CDECLS;

