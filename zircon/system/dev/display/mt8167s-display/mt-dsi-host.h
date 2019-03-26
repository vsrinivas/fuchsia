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
#include <ddktl/protocol/dsiimpl.h>
#include <lib/mmio/mmio.h>
#include <hwreg/mmio.h>
#include <fbl/unique_ptr.h>
#include <optional>
#include "common.h"
#include "mt-sysconfig.h"
#include "registers-mipiphy.h"
#include "lcd.h"

namespace mt8167s_display {

// [Ovl] --> [Clr] --> [Clr Correction] --> [AAL] --> [Gamma] --> [Dither] --> [RDMA] --> [DSI]

// The DSI engine is responsible for fetching data from the display pipe and outputting it to
// the MIPI PHY. The DSI IP is mediatek specific. However, it does follow the MIPI DSI SPEC. This
// class is responsible for setting up the MIPI-PHY and use the dsi-mt driver to perform
// DSI specific operations.

class MtDsiHost {
public:
    MtDsiHost(uint32_t height, uint32_t width, uint8_t panel_type)
        : height_(height), width_(width), panel_type_(panel_type) {
        ZX_ASSERT(height_ < kMaxHeight);
        ZX_ASSERT(width_ < kMaxWidth);
    }
    zx_status_t Init(zx_device_t* parent);
    zx_status_t Config(const display_setting_t& disp_setting);
    zx_status_t Start();
    zx_status_t Shutdown(fbl::unique_ptr<MtSysConfig>& syscfg);
    void PrintRegisters();

private:
    void ConfigMipiPll(uint32_t pll_clock, uint32_t lane_num);
    void PowerOffMipiTx();

    uint32_t height_; // display height
    uint32_t width_; // display width
    fbl::unique_ptr<ddk::MmioBuffer> mipi_tx_mmio_;
    pdev_protocol_t pdev_ = {nullptr, nullptr};
    zx::bti bti_;
    ddk::DsiImplProtocolClient dsiimpl_;
    fbl::unique_ptr<Lcd> lcd_;
    uint8_t panel_type_;
    bool initialized_ = false;
};

} // namespace mt8167s_display
