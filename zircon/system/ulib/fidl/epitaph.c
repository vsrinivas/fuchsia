// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifdef __Fuchsia__

#include <string.h>

#include <lib/fidl/epitaph.h>
#include <zircon/fidl.h>
#include <zircon/syscalls.h>

zx_status_t fidl_epitaph_write(zx_handle_t channel, zx_status_t error) {
    fidl_epitaph_t epitaph;
    memset(&epitaph, 0, sizeof(epitaph));
    epitaph.hdr.ordinal = FIDL_EPITAPH_ORDINAL;
    epitaph.hdr.reserved0 = error;

    return zx_channel_write(channel, 0, &epitaph, sizeof(epitaph), NULL, 0);
}

#endif // __Fuchsia__
