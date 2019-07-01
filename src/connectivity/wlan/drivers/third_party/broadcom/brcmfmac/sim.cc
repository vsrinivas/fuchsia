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
#include "chip.h"
#include "debug.h"
#include "device.h"

// Get device-specific information
zx_status_t brcmf_sim_probe(brcmf_simdev* simdev) {
    uint32_t chip, chiprev;

    simdev->sim_fw->getChipInfo(&chip, &chiprev);
    simdev->bus_if.chip = chip;
    simdev->bus_if.chiprev = chiprev;

    return ZX_OK;
}

// Allocate necessary memory and initialize simulator-specific structures
zx_status_t brcmf_sim_register(zx_device_t* zxdev) {
    zx_status_t status = ZX_OK;
    auto simdev = new brcmf_simdev {};
    struct brcmf_bus* bus_if = &simdev->bus_if;

    BRCMF_DBG(SIM, "Registering simulator target\n");

    simdev->sim_fw = std::make_unique<SimFirmware>();

    brcmf_device* dev = &simdev->dev;
    dev->zxdev = zxdev;
    bus_if->bus_priv.sim = simdev;
    dev->bus = bus_if;

    status = brcmf_sim_probe(simdev);
    if (status != ZX_OK) {
        BRCMF_ERR("sim_probe failed: %s\n", zx_status_get_string(status));
        dev->zxdev = nullptr;
        dev->bus = nullptr;
        delete simdev;
    }
    return status;
}

void brcmf_sim_exit(void) {
    // TODO (WLAN-1057): Free memory associated with the brcmf_simdev instance.
}
