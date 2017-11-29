// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include "display-device.h"

namespace i915 {

class HdmiDisplay : public DisplayDevice {
public:
    HdmiDisplay(Controller* controller, registers::Ddi ddi, registers::Pipe pipe);

private:
    bool Init(zx_display_info* info) final;
    bool I2cRead(uint32_t addr, uint8_t* buf, uint32_t size) final;
    bool I2cWrite(uint32_t addr, uint8_t* buf, uint32_t size) final;

    bool I2cFinish();
    bool I2cWaitForHwReady();
    bool I2cClearNack();
    bool I2cTransfer(uint32_t addr, uint8_t* buf, uint32_t size, bool read, bool allow_retry);
};

} // namespace i915
