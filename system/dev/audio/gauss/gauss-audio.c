// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <ddk/binding.h>
#include <ddk/debug.h>
#include <ddk/protocol/platform-defs.h>

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "gauss-audio.h"

static void gauss_audio_release(void* ctx) {
    gauss_audio_t* audio = ctx;
    // release other resources here
    free(audio);
}

static zx_protocol_device_t audio_device_proto = {
    .version = DEVICE_OPS_VERSION,
    .release = gauss_audio_release,
};

static zx_status_t gauss_audio_bind(void* ctx, zx_device_t* parent, void** cookie) {
    dprintf(INFO, "gauss_audio_bind\n");
    zx_status_t status;

    gauss_audio_t* audio = calloc(1, sizeof(gauss_audio_t));
    if (!audio) {
        status = ZX_ERR_NO_MEMORY;
        goto fail;
    }

    status = device_get_protocol(parent, ZX_PROTOCOL_PLATFORM_DEV, &audio->pdev);
    if (status != ZX_OK) {
        goto fail;
    }

    // more driver initialization will go here

    device_add_args_t args = {
        .version = DEVICE_ADD_ARGS_VERSION,
        .name = "gauss-audio",
        .ctx = audio,
        .ops = &audio_device_proto,
// add audio protocol here
//        .proto_id = ZX_PROTOCOL_AUDIO,
//        .proto_ops = &audio_protocol,
    };

    status = device_add(parent, &args, &audio->zxdev);
    if (status != ZX_OK) {
        goto fail;
    }

    return ZX_OK;

fail:
    dprintf(ERROR, "gauss_audio_bind failed %d\n", status);
    gauss_audio_release(audio);
    return status;
}

static zx_driver_ops_t audio_driver_ops = {
    .version = DRIVER_OPS_VERSION,
    .bind = gauss_audio_bind,
};

// clang-format off
ZIRCON_DRIVER_BEGIN(gauss_audio, audio_driver_ops, "zircon", "0.1", 3)
    BI_ABORT_IF(NE, BIND_PLATFORM_DEV_VID, PDEV_VID_AMLOGIC),
    BI_ABORT_IF(NE, BIND_PLATFORM_DEV_PID, PDEV_PID_AMLOGIC_A113),
    BI_MATCH_IF(EQ, BIND_PLATFORM_DEV_DID, PDEV_DID_AMLOGIC_GAUSS_AUDIO),
ZIRCON_DRIVER_END(gauss_audio)
// clang-format on
