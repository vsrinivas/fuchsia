// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <threads.h>
#include <unistd.h>

#include <ddk/binding.h>
#include <ddk/debug.h>
#include <ddk/device.h>
#include <ddk/driver.h>
#include <ddk/protocol/platform-defs.h>

#include <zircon/assert.h>
#include <zircon/process.h>
#include <zircon/syscalls.h>

#include <soc/aml-a113/a113-bus.h>
#include <soc/aml-a113/a113-hw.h>


zx_status_t a113_bus_init(a113_bus_t** out) {
    zx_status_t status;

    a113_bus_t* bus = calloc(1, sizeof(a113_bus_t));
    if (!bus) {
        return ZX_ERR_NO_MEMORY;
    }

    if ((status = a113_gpio_init(bus)) != ZX_OK) {
        zxlogf(ERROR, "a113_gpio_init failed: %d\n", status);
        goto fail;
    }
    if ((status = a113_i2c_init(bus)) != ZX_OK) {
        zxlogf(ERROR, "a113_i2c_init failed: %d\n", status);
        goto fail;
    }

    *out = bus;
    return ZX_OK;

fail:
    printf("a113_bus_init failed %d\n", status);
    a113_bus_release(bus);
    return status;
}

void a113_bus_release(a113_bus_t* bus) {
    a113_gpio_release(bus);
    free(bus);
}
