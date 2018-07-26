// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <string.h>
#include <zircon/fidl.h>
#include <zircon/syscalls.h>

zx_status_t fidl_epitaph_write(zx_handle_t channel, zx_status_t sys_error,
                               int32_t app_error) {
    fidl_epitaph_t epitaph;
    memset(&epitaph, 0, sizeof(epitaph));
    epitaph.hdr.ordinal = FIDL_EPITAPH_ORDINAL;
    epitaph.sys_error = sys_error;
    epitaph.app_error = app_error;

    return zx_channel_write(channel, 0, &epitaph, sizeof(epitaph), NULL, 0);
}
