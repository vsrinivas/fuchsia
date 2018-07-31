// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be found
// in the LICENSE file.

#include <ddk/device.h>
#include <zircon/types.h>

extern "C" zx_status_t rtl8821_bind(void* ctx, zx_device_t* device) {
    return ZX_OK;
}
