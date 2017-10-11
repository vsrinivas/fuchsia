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

#include "hw.h"

#include <ddk/device.h>
#include <ddk/driver.h>
#include <ddk/binding.h>

#include <zircon/types.h>

extern zx_status_t ath10k_bind(void* ctx, zx_device_t* device, void** cookie);

static zx_driver_ops_t ath10k_driver_ops = {
    .version = DRIVER_OPS_VERSION,
    .bind = ath10k_bind,
};

ZIRCON_DRIVER_BEGIN(ath10k, ath10k_driver_ops, "zircon", "0.1", 10)
    BI_ABORT_IF(NE, BIND_PROTOCOL, ZX_PROTOCOL_PCI),
    BI_ABORT_IF(NE, BIND_PCI_VID, ATHEROS_VID),
    BI_MATCH_IF(EQ, BIND_PCI_DID, QCA988X_2_0_DID),
    BI_MATCH_IF(EQ, BIND_PCI_DID, QCA6174_2_1_DID),
    BI_MATCH_IF(EQ, BIND_PCI_DID, QCA99X0_2_0_DID),
    BI_MATCH_IF(EQ, BIND_PCI_DID, QCA6164_2_1_DID),
    BI_MATCH_IF(EQ, BIND_PCI_DID, QCA9377_1_0_DID),
    BI_MATCH_IF(EQ, BIND_PCI_DID, QCA9984_1_0_DID),
    BI_MATCH_IF(EQ, BIND_PCI_DID, QCA9887_1_0_DID),
    BI_MATCH_IF(EQ, BIND_PCI_DID, QCA9888_2_0_DID),
ZIRCON_DRIVER_END(ath10k)
