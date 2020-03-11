// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DDK_FRAGMENT_DEVICE_H_
#define DDK_FRAGMENT_DEVICE_H_

#include <zircon/types.h>

#include <ddk/device.h>

// This header is only meant to be used in fragment driver which can maintain the protocol
// contexts. It is not needed/not intended to be used elsewhere.
// protocols look like: typedef struct
// {
//     protocol_xyz_ops_t* ops;
//     void* ctx;
// } protocol_xyz_t;
zx_status_t device_open_protocol_session_multibindable(const zx_device_t* dev, uint32_t proto_id,
                                                       void* protocol);
zx_status_t device_close_protocol_session_multibindable(const zx_device_t* dev, void* protocol_ctx);

#endif
