// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <ddktl/device.h>
#include <ddktl/protocol/display.h>

#include <zx/vmo.h>

#include "edid.h"
#include "registers-ddi.h"
#include "registers-pipe.h"

namespace i915 {

class Controller;

class DisplayDevice;
using DisplayDeviceType = ddk::Device<DisplayDevice>;

class DisplayDevice : public DisplayDeviceType, public ddk::DisplayProtocol<DisplayDevice> {
public:
    DisplayDevice(Controller* device, registers::Ddi ddi, registers::Pipe pipe);
    virtual ~DisplayDevice();

    void DdkRelease();

    zx_status_t SetMode(zx_display_info_t* info);
    zx_status_t GetMode(zx_display_info_t* info);
    zx_status_t GetFramebuffer(void** framebuffer);
    void Flush();

    bool Init();

    uintptr_t framebuffer() const { return framebuffer_; }
    uint32_t framebuffer_size() const { return framebuffer_size_; }
    const zx_display_info_t& info() const { return info_; }
    registers::Ddi ddi() const { return ddi_; }
    registers::Pipe pipe() const { return pipe_; }
    int dpll() const {
        // Skip over dpll0 because changing it requires messing around with CDCLK
        return pipe_ + 1;
    }

protected:
    virtual bool Init(zx_display_info_t* info) = 0;

    // Subclasses must implement these for LoadEdid to work
    virtual bool I2cRead(uint32_t addr, uint8_t* buf, uint32_t size) { return false; }
    virtual bool I2cWrite(uint32_t addr, uint8_t* buf, uint32_t size) { return false; }

    bool LoadEdid(registers::BaseEdid* edid);
    MmioSpace* mmio_space() const;
    bool EnablePowerWell2();

private:
    // This is the I2C address for DDC segment, for fetching EDID data.
    static constexpr int kDdcSegmentI2cAddress = 0x30;
    // This is the I2C address for DDC, for fetching EDID data.
    static constexpr int kDdcI2cAddress = 0x50;

    // Borrowed reference to Controller instance
    Controller* controller_;

    registers::Ddi ddi_;
    registers::Pipe pipe_;

    uintptr_t framebuffer_;
    uint32_t framebuffer_size_;
    zx::vmo framebuffer_vmo_;

    zx_display_info_t info_;
};

} // namespace i915
