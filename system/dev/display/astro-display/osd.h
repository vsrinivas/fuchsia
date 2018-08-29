// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <zircon/compiler.h>
#include <ddk/protocol/platform-device.h>
#include <fbl/unique_ptr.h>
#include <hwreg/mmio.h>
#include "vpu.h"
#include "common.h"

namespace astro_display {

class Osd {
public:
    Osd() {}
    ~Osd(){ io_buffer_release(&mmio_vpu_); }
    zx_status_t Init(zx_device_t* parent);
    zx_status_t Configure();
    void Disable(void);
    void Flip(uint8_t idx);
    void PrintRegs(void);

private:
    void Enable(void);

    io_buffer_t                         mmio_vpu_;
    platform_device_protocol_t          pdev_ = {nullptr, nullptr};
    fbl::unique_ptr<hwreg::RegisterIo>  vpu_regs_;
};

} // namespace astro_display
