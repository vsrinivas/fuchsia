// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once
#include <lib/zx/bti.h>
#include <zircon/compiler.h>
#include <ddk/protocol/platform/device.h>
#include <ddk/protocol/platform-device-lib.h>
#include <zircon/assert.h>
#include <ddktl/device.h>
#include <lib/mmio/mmio.h>
#include <hwreg/mmio.h>
#include <fbl/unique_ptr.h>
#include <optional>
#include "common.h"
#include "registers-gamma.h"

namespace mt8167s_display {

// [Ovl] --> [Color] --> [CCorr] --> [AAL] --> [Gamma] --> [Dither] --> [RDMA] --> [DSI]
//
// Gamma engine changes the overall misture of RGB color to fit the characteristics of the target
// panel

class Gamma {
public:
    Gamma(uint32_t height, uint32_t width)
        : height_(height), width_(width) {
        ZX_ASSERT(height_ < kMaxHeight);
        ZX_ASSERT(width_ < kMaxWidth);
    }
    zx_status_t Init(zx_device_t* parent);
    zx_status_t Config();
    void PrintRegisters();

private:
    fbl::unique_ptr<ddk::MmioBuffer> gamma_mmio_;
    pdev_protocol_t pdev_ = {nullptr, nullptr};
    const uint32_t height_; // Display height
    const uint32_t width_; // Display width
    bool initialized_ = false;
};

} // namespace mt8167s_display
