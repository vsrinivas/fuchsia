// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <ddk/device.h>
#include <ddk/driver.h>
#include <ddk/binding.h>
#include <ddk/protocol/pci.h>

extern mx_status_t qemu_ihda_codec_bind_hook(mx_driver_t*, mx_device_t*, void**);
extern void        qemu_ihda_codec_unbind_hook(mx_driver_t*, mx_device_t*, void*);

mx_driver_t _driver_qemu_ihda_codec = {
    .ops = {
        .init    = NULL,
        .bind    = qemu_ihda_codec_bind_hook,
        .unbind  = qemu_ihda_codec_unbind_hook,
        .release = NULL,
    },
};

MAGENTA_DRIVER_BEGIN(_driver_qemu_ihda_codec, "qemu-ihda-codec", "magenta", "0.1", 3)
    BI_ABORT_IF(NE, BIND_PROTOCOL, MX_PROTOCOL_IHDA_CODEC),
    BI_ABORT_IF(NE, BIND_IHDA_CODEC_VID, 0x1af4),
    BI_MATCH_IF(EQ, BIND_IHDA_CODEC_DID, 0x0022),
MAGENTA_DRIVER_END(_driver_qemu_ihda_codec)
