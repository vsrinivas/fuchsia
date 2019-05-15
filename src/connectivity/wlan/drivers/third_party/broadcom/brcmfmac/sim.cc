/*
 * Copyright (c) 2019 The Fuchsia Authors.
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include "sim.h"

#include <ddk/device.h>
#include <zircon/status.h>

#include "bus.h"
#include "device.h"

// Start talking to simulated firmware
zx_status_t brcmf_sim_probe(struct brcmf_simdev* simdev) {
    return ZX_OK;
}

// Allocate necessary memory and initialize simulator-specific structures
zx_status_t brcmf_sim_register(zx_device_t* zxdev) {
    struct brcmf_bus* bus_if = nullptr;
    struct brcmf_simdev* simdev = nullptr;
    struct brcmf_device* dev = nullptr;
    zx_status_t status = ZX_OK;

    brcmf_dbg(SIM, "Registering simulator target\n");

    bus_if = static_cast<decltype(bus_if)>(calloc(1, sizeof(struct brcmf_bus)));
    if (!bus_if) {
        status = ZX_ERR_NO_MEMORY;
        goto fail;
    }

    simdev = static_cast<decltype(simdev)>(calloc(1, sizeof(struct brcmf_simdev)));
    if (!bus_if) {
        status = ZX_ERR_NO_MEMORY;
        goto fail_bus;
    }

    dev = &simdev->dev;
    dev->zxdev = zxdev;
    simdev->bus_if = bus_if;
    bus_if->bus_priv.sim = simdev;
    bus_if->proto_type = BRCMF_PROTO_BCDC;
    dev->bus = bus_if;

    status = brcmf_sim_probe(simdev);
    if (status != ZX_OK) {
        brcmf_err("sim_probe failed: %s\n", zx_status_get_string(status));
        goto fail_simdev;
    }
    return ZX_OK;

fail_simdev:
    free(simdev);
    dev->zxdev = NULL;
    dev->bus = NULL;

fail_bus:
    free(bus_if);

fail:
    return status;
}

void brcmf_sim_exit(void) {
}
