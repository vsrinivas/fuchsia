// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <ddk/debug.h>
#include <ddk/protocol/platform-defs.h>
#include <hw/reg.h>

#include "a113-bus.h"

static const pbus_dev_t gauss_audio_dev = {
    .name = "gauss-audio",
    .vid = PDEV_VID_AMLOGIC,
    .pid = PDEV_PID_AMLOGIC_A113,
    .did = PDEV_DID_AMLOGIC_GAUSS_AUDIO,
};

zx_status_t a113_audio_init(a113_bus_t* bus) {
    zx_status_t status;

    // Various hardware initialization and configuration will go here

    if ((status = pbus_device_add(&bus->pbus, &gauss_audio_dev, 0)) != ZX_OK) {
        zxlogf(ERROR, "a113_audio_init could not add gauss_audio_dev: %d\n", status);
    }

    return status;
}
