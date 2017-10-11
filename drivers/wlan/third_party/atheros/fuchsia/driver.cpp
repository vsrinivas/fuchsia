/*
 * Copyright (c) 2017 The Fuchsia Authors.
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

#include "device.h"
#include "pci.h"

#include <ddk/device.h>
#include <ddk/driver.h>
#include <fbl/unique_ptr.h>

#include <cstdio>

extern "C" zx_status_t ath10k_bind(void* ctx, zx_device_t* device, void** cookie) {
    std::printf("%s\n", __func__);

    pci_protocol_t pci;
    zx_status_t status =
        device_get_protocol(device, ZX_PROTOCOL_PCI, reinterpret_cast<void*>(&pci));
    // For now, we only support PCIe devices, so just return an error in this case.
    if (status != ZX_OK) {
        return status;
    }

    auto pcibus = fbl::make_unique<ath10k::PciBus>(&pci);
    auto dev = fbl::make_unique<ath10k::Device>(device, fbl::move(pcibus));
    status = dev->Bind();
    if (status != ZX_OK) {
        std::printf("ath10k: could not bind: %d\n", status);
    } else {
        // devhost is now responsible for the memory used by dev. It will be
        // cleaned up in the Device::Release() method.
        dev.release();
    }

    return status;
}
