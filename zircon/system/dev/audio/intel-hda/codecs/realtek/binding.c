// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <ddk/device.h>
#include <ddk/driver.h>
#include <ddk/binding.h>
#include <ddk/protocol/pci.h>

extern zx_status_t realtek_ihda_codec_bind_hook(void*, zx_device_t*);

static zx_driver_ops_t realtek_ihda_codec_driver_ops = {
    .version = DRIVER_OPS_VERSION,
    .init    = NULL,
    .bind    = realtek_ihda_codec_bind_hook,
    .release = NULL,
};

ZIRCON_DRIVER_BEGIN(realtek_ihda_codec, realtek_ihda_codec_driver_ops, "zircon", "0.1", 4)
    BI_ABORT_IF(NE, BIND_PROTOCOL, ZX_PROTOCOL_IHDA_CODEC),
    BI_ABORT_IF(NE, BIND_IHDA_CODEC_VID, 0x10ec),   // Realtek
    BI_MATCH_IF(EQ, BIND_IHDA_CODEC_DID, 0x0255),   // ALC255
    BI_MATCH_IF(EQ, BIND_IHDA_CODEC_DID, 0x0283),   // ALC283
ZIRCON_DRIVER_END(realtek_ihda_codec)
