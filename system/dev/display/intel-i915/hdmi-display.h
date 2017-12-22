// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include "display-device.h"
#include "edid.h"

namespace i915 {

class HdmiDisplay : public DisplayDevice, private edid::EdidSource {
public:
    HdmiDisplay(Controller* controller, registers::Ddi ddi, registers::Pipe pipe);

private:
    bool Init(zx_display_info* info) final;
    bool ReadEdid(uint8_t segment, uint8_t offset, uint8_t* buf, uint8_t len) final;

    bool I2cFinish();
    bool I2cWaitForHwReady();
    bool I2cClearNack();
    bool SetDdcSegment(uint8_t block_num);
    bool GMBusRead(uint8_t addr, uint8_t* buf, uint8_t size);
    bool GMBusWrite(uint8_t addr, uint8_t* buf, uint8_t size);
};

} // namespace i915
