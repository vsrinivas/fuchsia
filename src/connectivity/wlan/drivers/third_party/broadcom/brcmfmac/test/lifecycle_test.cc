/*
 * Copyright (c) 2019 The Fuchsia Authors
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY
 * SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN ACTION
 * OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF OR IN
 * CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include <ddk/device.h>
#include <gtest/gtest.h>
#include <stdio.h>

#include "src/connectivity/wlan/drivers/third_party/broadcom/brcmfmac/common.h"

namespace {

static zx_device_t* sim_dev = reinterpret_cast<zx_device_t*>(0x123456543210);

TEST(LifecycleTest, StartStop) {
    zx_status_t status = brcmfmac_module_init(sim_dev);
    EXPECT_EQ(status, ZX_OK);
    brcmf_core_exit();
}

}  // namespace
